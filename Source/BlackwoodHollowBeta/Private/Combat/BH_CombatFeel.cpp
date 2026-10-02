// Blackwood Hollow - combat "juice" (implementation)

#include "Combat/BH_CombatFeel.h"
#include "Combat/BH_CombatIdentityComponent.h"
#include "Cues/BH_CameraShakes.h"
#include "Cues/BH_CueUtils.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PointLightComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Sound/SoundBase.h"

DEFINE_LOG_CATEGORY(LogBHFeel);

static TAutoConsoleVariable<float> CVarBHShakeScale(
	TEXT("bh.Combat.ShakeScale"),
	1.f,
	TEXT("Global multiplier for combat camera shakes (0 = off). Accessibility toggle hook."),
	ECVF_Default);

namespace
{
	const TCHAR* TierName(EBH_ImpactTier Tier)
	{
		switch (Tier)
		{
		case EBH_ImpactTier::Light: return TEXT("Light");
		case EBH_ImpactTier::Medium: return TEXT("Medium");
		case EBH_ImpactTier::Heavy: return TEXT("Heavy");
		default: return TEXT("Massive");
		}
	}

	/** The local player controller that has a pawn (first one found); used for spectator falloff. */
	APlayerController* FindAnyLocalController(const UWorld* World)
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* PC = It->Get();
			if (PC && PC->IsLocalController() && PC->PlayerCameraManager)
			{
				return PC;
			}
		}
		return nullptr;
	}

	void SpawnFlash(UWorld* World, const FVector& Location, const FBH_ImpactTierSettings& Tier)
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Holder = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Location), Params);
		if (!Holder)
		{
			return;
		}
		UPointLightComponent* Light = NewObject<UPointLightComponent>(Holder, TEXT("ImpactFlash"));
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetIntensity(Tier.FlashIntensity);
		Light->SetLightColor(Tier.FlashColor);
		Light->SetAttenuationRadius(Tier.FlashRadius);
		Light->SetCastShadows(false);
		Holder->SetRootComponent(Light);
		Light->RegisterComponent();
		Holder->SetLifeSpan(0.06f);
	}

	TMap<TWeakObjectPtr<const AActor>, double> GLastVoiceTime[3]; // [0] hurt group, [1] death, [2] everything else
}

// ============================================================================
// Data
// ============================================================================

const TArray<TObjectPtr<USoundBase>>& UBH_VoiceSetDataAsset::GetSounds(EBH_VoiceCategory Category) const
{
	switch (Category)
	{
	case EBH_VoiceCategory::AttackLight: return AttackLight;
	case EBH_VoiceCategory::AttackHeavy: return AttackHeavy;
	case EBH_VoiceCategory::Hurt: return Hurt;
	case EBH_VoiceCategory::HurtHeavy: return HurtHeavy;
	case EBH_VoiceCategory::PostureBreak: return PostureBreak;
	case EBH_VoiceCategory::Dodge: return Dodge;
	default: return Death;
	}
}

UBH_CombatFeelSettings::UBH_CombatFeelSettings()
{
	Tiers.SetNum(4);

	Tiers[0].ShakeClass = UBH_CameraShake_Light::StaticClass();
	Tiers[0].HitStopDuration = 0.03f;

	Tiers[1].ShakeClass = UBH_CameraShake_Medium::StaticClass();
	Tiers[1].HitStopDuration = 0.05f;

	Tiers[2].ShakeClass = UBH_CameraShake_Heavy::StaticClass();
	Tiers[2].HitStopDuration = 0.08f;
	Tiers[2].FlashIntensity = 2500.f;

	Tiers[3].ShakeClass = UBH_CameraShake_Massive::StaticClass();
	Tiers[3].HitStopDuration = 0.12f;
	Tiers[3].FlashIntensity = 6000.f;
	Tiers[3].FlashColor = FLinearColor(0.75f, 0.6f, 1.f);
	Tiers[3].FlashRadius = 500.f;

	TrailLifetimes.Add(TEXT("Greatsword"), 0.18f);
}

const UBH_CombatFeelSettings* UBH_CombatFeelSettings::Get()
{
	static TWeakObjectPtr<UBH_CombatFeelSettings> Cached;
	if (!Cached.IsValid())
	{
		UBH_CombatFeelSettings* Loaded = LoadObject<UBH_CombatFeelSettings>(nullptr, TEXT("/Game/BlackwoodHollow/Combat/Data/DA_CombatFeel.DA_CombatFeel"), nullptr, LOAD_NoWarn);
		if (!Loaded)
		{
			Loaded = NewObject<UBH_CombatFeelSettings>(GetTransientPackage(), TEXT("DefaultCombatFeel"));
		}
		Loaded->AddToRoot();
		Cached = Loaded;
	}
	return Cached.Get();
}

// ============================================================================
// Library
// ============================================================================

EBH_ImpactTier UBH_CombatFeelLibrary::TierForHit(float Damage, float PostureMultiplier, bool bBlocked, bool bBlockerStaminaZero)
{
	const UBH_CombatFeelSettings* Settings = UBH_CombatFeelSettings::Get();
	if (bBlocked)
	{
		return bBlockerStaminaZero ? EBH_ImpactTier::Medium : EBH_ImpactTier::Light;
	}
	if (PostureMultiplier > Settings->FinisherPostureMultiplier)
	{
		return EBH_ImpactTier::Heavy;
	}
	if (Damage < Settings->LightDamageMax)
	{
		return EBH_ImpactTier::Light;
	}
	return Damage < Settings->MediumDamageMax ? EBH_ImpactTier::Medium : EBH_ImpactTier::Heavy;
}

EBH_ImpactTier UBH_CombatFeelLibrary::Escalate(EBH_ImpactTier Tier)
{
	return static_cast<EBH_ImpactTier>(FMath::Min(static_cast<int32>(Tier) + 1, static_cast<int32>(EBH_ImpactTier::Massive)));
}

void UBH_CombatFeelLibrary::PlayImpactFeel(const UObject* WorldContext, EBH_ImpactTier Tier, AActor* Instigator, AActor* Victim, FVector Location,
	bool bEscalateForVictim, bool bInstigatorOnlyShake)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const UBH_CombatFeelSettings* Settings = UBH_CombatFeelSettings::Get();
	if (!Settings->Tiers.IsValidIndex(static_cast<int32>(Tier)))
	{
		return;
	}
	const FBH_ImpactTierSettings& TierData = Settings->Tiers[static_cast<int32>(Tier)];

	// Hit-stop: both fighters, every machine (cosmetic).
	if (TierData.HitStopDuration > 0.f)
	{
		BH_CueUtils::ApplyHitStop(Instigator, TierData.HitStopDuration);
		if (Victim != Instigator)
		{
			BH_CueUtils::ApplyHitStop(Victim, TierData.HitStopDuration);
		}
	}

	// Impact flash (Heavy and above only: their FlashIntensity is non-zero by default).
	if (TierData.FlashIntensity > 0.f)
	{
		SpawnFlash(World, Location, TierData);
	}

	// Camera shake for the local player.
	EBH_ImpactTier ShakeTier = Tier;
	float SpectatorScale = 1.f;
	APlayerController* PC = bInstigatorOnlyShake ? BH_CueUtils::FindLocalControllerInvolving(Instigator, nullptr) : BH_CueUtils::FindLocalControllerInvolving(Instigator, Victim);
	const bool bLocalInvolved = PC != nullptr;
	if (PC)
	{
		if (bEscalateForVictim && Victim && PC->GetPawn() == Victim)
		{
			ShakeTier = Escalate(Tier);
		}
	}
	else if (!bInstigatorOnlyShake)
	{
		PC = FindAnyLocalController(World);
		if (PC)
		{
			const FVector ViewLocation = PC->PlayerCameraManager->GetCameraLocation();
			const float Distance = static_cast<float>(FVector::Dist(ViewLocation, Location));
			if (Settings->SpectatorShakeRadius <= 0.f || Distance >= Settings->SpectatorShakeRadius)
			{
				PC = nullptr;
			}
			else
			{
				SpectatorScale = Settings->SpectatorShakeScale * (1.f - Distance / Settings->SpectatorShakeRadius);
			}
		}
	}

	const FBH_ImpactTierSettings& ShakeData = Settings->Tiers[static_cast<int32>(ShakeTier)];
	const float Scale = ShakeData.ShakeScale * Settings->ShakeScaleMultiplier * CVarBHShakeScale.GetValueOnGameThread() * SpectatorScale;

	UCameraShakeBase* Shake = nullptr;
	if (PC && PC->PlayerCameraManager && ShakeData.ShakeClass && Scale > 0.f)
	{
		FVector Direction = FVector::ZeroVector;
		if (Instigator && Victim)
		{
			Direction = Victim->GetActorLocation() - Instigator->GetActorLocation();
		}
		else if (Instigator || Victim)
		{
			Direction = Location - (Instigator ? Instigator : Victim)->GetActorLocation();
		}
		if (Direction.SizeSquared2D() > KINDA_SMALL_NUMBER)
		{
			Shake = PC->PlayerCameraManager->StartCameraShake(ShakeData.ShakeClass, Scale, ECameraShakePlaySpace::UserDefined, FRotator(0.f, Direction.Rotation().Yaw, 0.f));
		}
		else
		{
			Shake = PC->PlayerCameraManager->StartCameraShake(ShakeData.ShakeClass, Scale, ECameraShakePlaySpace::CameraLocal);
		}
	}

	UE_LOG(LogBHFeel, Verbose, TEXT("PlayImpactFeel: tier=%s shakeTier=%s instigator=%s victim=%s hitstop=%.2f flash=%.0f localInvolved=%d shake=%s scale=%.2f"),
		TierName(Tier), TierName(ShakeTier), *GetNameSafe(Instigator), *GetNameSafe(Victim), TierData.HitStopDuration, TierData.FlashIntensity,
		bLocalInvolved ? 1 : 0, Shake ? *Shake->GetClass()->GetName() : TEXT("none"), Scale);
}

const UBH_VoiceSetDataAsset* UBH_CombatFeelLibrary::ResolveVoiceSet(const AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}
	if (const UBH_CombatIdentityComponent* Identity = UBH_CombatIdentityComponent::Find(Actor))
	{
		if (Identity->VoiceSet)
		{
			return Identity->VoiceSet;
		}
	}
	const APawn* Pawn = Cast<APawn>(Actor);
	if (Pawn && Pawn->GetPlayerState())
	{
		return UBH_CombatFeelSettings::Get()->PlayerVoiceSet;
	}
	return nullptr;
}

USoundBase* UBH_CombatFeelLibrary::PlayVoice(AActor* Actor, EBH_VoiceCategory Category, float Chance)
{
	UWorld* World = Actor ? Actor->GetWorld() : nullptr;
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return nullptr;
	}

	const UBH_CombatFeelSettings* Settings = UBH_CombatFeelSettings::Get();
	const UEnum* CategoryEnum = StaticEnum<EBH_VoiceCategory>();
	const FString CategoryName = CategoryEnum ? CategoryEnum->GetNameStringByValue(static_cast<int64>(Category)) : FString();

	if (Chance < 1.f && FMath::FRand() > Chance)
	{
		UE_LOG(LogBHFeel, Verbose, TEXT("Voice: owner=%s cat=%s -> skipped by chance (%.2f)"), *GetNameSafe(Actor), *CategoryName, Chance);
		return nullptr;
	}

	const bool bHurt = Category == EBH_VoiceCategory::Hurt || Category == EBH_VoiceCategory::HurtHeavy;
	const bool bDeath = Category == EBH_VoiceCategory::Death;
	const int32 Group = bHurt ? 0 : (bDeath ? 1 : 2);
	const float Interval = bHurt ? Settings->HurtVoiceInterval : (bDeath ? 2.f : Settings->VoiceMinInterval);
	const double Now = World->GetTimeSeconds();
	{
		TMap<TWeakObjectPtr<const AActor>, double>& Times = GLastVoiceTime[Group];
		if (Times.Num() > 32)
		{
			for (auto It = Times.CreateIterator(); It; ++It)
			{
				if (!It.Key().IsValid() || Now - It.Value() > 5.0 || It.Value() > Now)
				{
					It.RemoveCurrent();
				}
			}
		}
		double& Last = Times.FindOrAdd(Actor, -1.0e9);
		if (Now >= Last && Now - Last < Interval)
		{
			UE_LOG(LogBHFeel, Verbose, TEXT("Voice: owner=%s cat=%s -> rate-limited (%.2fs < %.2fs)"), *GetNameSafe(Actor), *CategoryName, Now - Last, Interval);
			return nullptr;
		}
		Last = Now;
	}

	const UBH_VoiceSetDataAsset* Set = ResolveVoiceSet(Actor);
	if (!Set || Set->GetSounds(Category).Num() == 0)
	{
		UE_LOG(LogBHFeel, Verbose, TEXT("Voice: owner=%s cat=%s set=%s -> no sounds authored (TODO)"), *GetNameSafe(Actor), *CategoryName, *GetNameSafe(Set));
		return nullptr;
	}

	const FVector Location = Actor->GetActorLocation() + FVector(0.0, 0.0, 80.0);
	const float Pitch = Set->PitchMultiplier;
	USoundBase* Played = BH_CueUtils::PlayRandomSound(Actor, Set->GetSounds(Category), Location,
		FVector2D(0.9f * Set->VolumeMultiplier, 1.f * Set->VolumeMultiplier), FVector2D(0.95f * Pitch, 1.05f * Pitch), Settings->VoiceAttenuation);
	UE_LOG(LogBHFeel, Verbose, TEXT("Voice: owner=%s cat=%s set=%s sound=%s"), *GetNameSafe(Actor), *CategoryName, *GetNameSafe(Set), *GetNameSafe(Played));
	return Played;
}
