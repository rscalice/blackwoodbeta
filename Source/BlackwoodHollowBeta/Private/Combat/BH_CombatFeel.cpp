// Blackwood Hollow - combat "juice" (implementation)

#include "Combat/BH_CombatFeel.h"
#include "Combat/BH_CombatIdentityComponent.h"
#include "Combat/BH_StanceComponent.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Characters/BH_CharacterBase.h"
#include "Cues/BH_CameraShakes.h"
#include "Cues/BH_CueUtils.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PointLightComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/RootMotionSource.h"
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

static TAutoConsoleVariable<int32> CVarBHCombatFeelDebug(
	TEXT("bh.CombatFeel.Debug"),
	0,
	TEXT("1 = log every controller rumble, parry punch (FOV kick) and directional shake at Log level (LogBHFeel). For testing."),
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

	// Animation freeze (hit-stop) and pushback: separate properties (not Tiers[]) so an already-saved DA_CombatFeel picks up these defaults.
	FreezeDurationByTier = { 0.06f, 0.09f, 0.13f, 0.18f };
	PushbackDistanceByTier = { 25.f, 50.f, 110.f, 180.f };
	PushbackDurationByTier = { 0.12f, 0.15f, 0.2f, 0.25f };
	PushbackStanceMultipliers.Add(TAG_Stance_Weapon_Greatsword, 1.4f);
	PushbackStanceMultipliers.Add(TAG_Stance_Weapon_DualSword, 0.8f);
	PushbackStanceMultipliers.Add(TAG_Stance_Weapon_SwordShield, 1.f);

	// Directional shake, parry punch and rumble (new properties: an already-saved DA_CombatFeel picks these defaults up).
	ParryPunchShake = UBH_CameraShake_ParryPunch::StaticClass();
	ParryFOVShake = UBH_CameraShake_ParryFOV::StaticClass();
	// Large = low-frequency motors, Small = high-frequency ones, Duration in seconds.
	HitDealtRumbleByTier = {
		FBH_RumbleSpec(0.00f, 0.25f, 0.06f), // Light
		FBH_RumbleSpec(0.25f, 0.35f, 0.09f), // Medium
		FBH_RumbleSpec(0.55f, 0.50f, 0.14f), // Heavy
		FBH_RumbleSpec(0.90f, 0.80f, 0.20f), // Massive (not used for dealt hits; posture breaks use PostureBreakRumble)
	};
	ParryRumble = FBH_RumbleSpec(0.70f, 1.00f, 0.12f);
	DamageTakenRumble = FBH_RumbleSpec(0.60f, 0.50f, 0.28f);
	PostureBreakRumble = FBH_RumbleSpec(1.00f, 0.90f, 0.45f);

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

float UBH_CombatFeelSettings::GetFreezeDuration(EBH_ImpactTier Tier) const
{
	const int32 Index = static_cast<int32>(Tier);
	if (FreezeDurationByTier.IsValidIndex(Index))
	{
		return FreezeDurationByTier[Index];
	}
	return Tiers.IsValidIndex(Index) ? Tiers[Index].HitStopDuration : 0.f;
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

void UBH_CombatFeelLibrary::ApplyTierFreeze(AActor* Actor, EBH_ImpactTier Tier, float Factor, bool bBlocked)
{
	const UBH_CombatFeelSettings* Settings = UBH_CombatFeelSettings::Get();
	if (!Actor || !Settings->bEnableAnimFreeze)
	{
		return;
	}
	const float Duration = Settings->GetFreezeDuration(Tier) * Factor * (bBlocked ? Settings->FreezeBlockedFactor : 1.f);
	BH_CueUtils::ApplyHitStop(Actor, Duration, Settings->FreezeAnimRateScale);
}

void UBH_CombatFeelLibrary::PlayImpactFeel(const UObject* WorldContext, EBH_ImpactTier Tier, AActor* Instigator, AActor* Victim, FVector Location,
	bool bEscalateForVictim, bool bInstigatorOnlyShake, bool bBlocked, bool bSkipFreeze)
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

	// Animation freeze (hit-stop): both fighters, every machine (cosmetic; attacker x1.0, victim x1.15 by default).
	if (!bSkipFreeze)
	{
		ApplyTierFreeze(Instigator, Tier, Settings->FreezeAttackerFactor, bBlocked);
		if (Victim != Instigator)
		{
			ApplyTierFreeze(Victim, Tier, Settings->FreezeVictimFactor, bBlocked);
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

	// Only when the local player is in the exchange (not a spectator): a direction-biased shake on top of the tier shake, and rumble.
	if (PC && bLocalInvolved && !bInstigatorOnlyShake)
	{
		const bool bLocalIsVictim = Victim && Victim != Instigator && PC->GetPawn() == Victim;
		FVector HitDirection = FVector::ZeroVector; // direction the hit travels (attacker -> victim)
		if (Instigator && Victim)
		{
			HitDirection = Victim->GetActorLocation() - Instigator->GetActorLocation();
		}
		else if (Instigator || Victim)
		{
			HitDirection = Location - (Instigator ? Instigator : Victim)->GetActorLocation();
		}
		if (Tier != EBH_ImpactTier::Massive && Settings->DirectionalShakeScale > 0.f)
		{
			// A taken hit comes FROM behind its travel direction; a dealt hit is in front of the camera along it.
			PlayDirectionalBiasShake(PC, bLocalIsVictim ? -HitDirection : HitDirection, Scale * Settings->DirectionalShakeScale);
		}

		if (Tier == EBH_ImpactTier::Massive)
		{
			PlayRumble(PC, Settings->PostureBreakRumble); // posture broken: caused or suffered
		}
		else if (bBlocked)
		{
			if (Settings->HitDealtRumbleByTier.IsValidIndex(0))
			{
				PlayRumble(PC, Settings->HitDealtRumbleByTier[0]);
			}
		}
		else if (bLocalIsVictim)
		{
			PlayRumble(PC, Settings->DamageTakenRumble);
		}
		else
		{
			const int32 TierIndex = FMath::Min(static_cast<int32>(Tier), 2); // Light..Heavy
			if (Settings->HitDealtRumbleByTier.IsValidIndex(TierIndex))
			{
				PlayRumble(PC, Settings->HitDealtRumbleByTier[TierIndex]);
			}
		}
	}

	UE_LOG(LogBHFeel, Verbose, TEXT("PlayImpactFeel: tier=%s shakeTier=%s instigator=%s victim=%s freeze=%.2f blocked=%d skipFreeze=%d flash=%.0f localInvolved=%d shake=%s scale=%.2f"),
		TierName(Tier), TierName(ShakeTier), *GetNameSafe(Instigator), *GetNameSafe(Victim), Settings->GetFreezeDuration(Tier), bBlocked ? 1 : 0, bSkipFreeze ? 1 : 0, TierData.FlashIntensity,
		bLocalInvolved ? 1 : 0, Shake ? *Shake->GetClass()->GetName() : TEXT("none"), Scale);
}

// ----------------------------------------------------------------------------
// Directional shake / parry punch / rumble
// ----------------------------------------------------------------------------

UCameraShakeBase* UBH_CombatFeelLibrary::PlayDirectionalBiasShake(APlayerController* PC, const FVector& FromDirection, float Scale)
{
	if (!PC || !PC->IsLocalController() || !PC->PlayerCameraManager || Scale <= 0.f || FromDirection.SizeSquared2D() <= KINDA_SMALL_NUMBER)
	{
		return nullptr;
	}
	const UBH_CombatFeelSettings* Settings = UBH_CombatFeelSettings::Get();

	// Which side of the camera is the hit coming from? (camera right axis . direction to the source, flattened)
	const FRotator CameraYaw(0.f, PC->PlayerCameraManager->GetCameraRotation().Yaw, 0.f);
	const FVector CameraRight = FRotationMatrix(CameraYaw).GetUnitAxis(EAxis::Y);
	const float Lateral = static_cast<float>(FVector::DotProduct(CameraRight, FromDirection.GetSafeNormal2D()));

	TSubclassOf<UCameraShakeBase> ShakeClass = UBH_CameraShake_HitFromFront::StaticClass();
	const TCHAR* SideName = TEXT("front");
	if (Lateral > Settings->DirectionalFrontalThreshold)
	{
		ShakeClass = UBH_CameraShake_HitFromRight::StaticClass();
		SideName = TEXT("right");
	}
	else if (Lateral < -Settings->DirectionalFrontalThreshold)
	{
		ShakeClass = UBH_CameraShake_HitFromLeft::StaticClass();
		SideName = TEXT("left");
	}

	UCameraShakeBase* Shake = PC->PlayerCameraManager->StartCameraShake(ShakeClass, Scale, ECameraShakePlaySpace::CameraLocal);
	if (CVarBHCombatFeelDebug.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogBHFeel, Log, TEXT("DirectionalShake: side=%s lateral=%.2f scale=%.2f class=%s"), SideName, Lateral, Scale, *ShakeClass->GetName());
	}
	return Shake;
}

void UBH_CombatFeelLibrary::PlayRumble(APlayerController* PC, const FBH_RumbleSpec& Spec)
{
	const UBH_CombatFeelSettings* Settings = UBH_CombatFeelSettings::Get();
	if (!PC || !PC->IsLocalController() || !Settings->bEnableRumble || Spec.Duration <= 0.f || (Spec.Large <= 0.f && Spec.Small <= 0.f))
	{
		return;
	}
	// Two dynamic actions: the low-frequency motors (left + right large) and the high-frequency ones (left + right small).
	if (Spec.Large > 0.f)
	{
		PC->PlayDynamicForceFeedback(Spec.Large, Spec.Duration, true, false, true, false);
	}
	if (Spec.Small > 0.f)
	{
		PC->PlayDynamicForceFeedback(Spec.Small, Spec.Duration, false, true, false, true);
	}
	if (CVarBHCombatFeelDebug.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogBHFeel, Log, TEXT("Rumble: pc=%s large=%.2f small=%.2f duration=%.2f"), *GetNameSafe(PC), Spec.Large, Spec.Small, Spec.Duration);
	}
}

void UBH_CombatFeelLibrary::PlayParryPunch(APlayerController* PC)
{
	if (!PC || !PC->IsLocalController() || !PC->PlayerCameraManager)
	{
		return;
	}
	const UBH_CombatFeelSettings* Settings = UBH_CombatFeelSettings::Get();
	const float Master = Settings->ShakeScaleMultiplier * CVarBHShakeScale.GetValueOnGameThread(); // accessibility: 0 turns the whole punch off

	if (Master > 0.f)
	{
		if (Settings->ParryPunchShake && Settings->ParryPunchShakeScale > 0.f)
		{
			PC->PlayerCameraManager->StartCameraShake(Settings->ParryPunchShake, Settings->ParryPunchShakeScale * Master, ECameraShakePlaySpace::CameraLocal);
		}
		if (Settings->ParryFOVShake && Settings->ParryFOVKickDegrees > 0.f)
		{
			// The shake's amplitude is -1 degree of FOV, so its scale is the kick in degrees.
			PC->PlayerCameraManager->StartCameraShake(Settings->ParryFOVShake, Settings->ParryFOVKickDegrees * Master, ECameraShakePlaySpace::CameraLocal);
		}
	}
	PlayRumble(PC, Settings->ParryRumble);
	if (CVarBHCombatFeelDebug.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogBHFeel, Log, TEXT("ParryPunch: pc=%s shake=%s x%.2f fovKick=%.1f deg master=%.2f"), *GetNameSafe(PC),
			*GetNameSafe(Settings->ParryPunchShake), Settings->ParryPunchShakeScale, Settings->ParryFOVKickDegrees, Master);
	}
}

// ----------------------------------------------------------------------------
// Pushback
// ----------------------------------------------------------------------------

bool UBH_CombatFeelLibrary::ApplyPushbackSource(ACharacter* Character, const FVector& Direction, float Distance, float Duration, uint16 Id)
{
	UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	const FVector FlatDirection = Direction.GetSafeNormal2D();
	if (!Movement || FlatDirection.IsNearlyZero() || Distance <= 0.f || Duration <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	// Constant force: velocity = Distance / Duration for Duration seconds, so the pawn travels about Distance. Additive, so it stacks
	// with montage root motion and normal input instead of replacing them; zero Z, and Z is ignored when accumulating.
	TSharedPtr<FRootMotionSource_ConstantForce> Source = MakeShared<FRootMotionSource_ConstantForce>();
	Source->InstanceName = FName(*FString::Printf(TEXT("BH_Pushback_%u"), static_cast<uint32>(Id)));
	Source->AccumulateMode = ERootMotionAccumulateMode::Additive;
	Source->Priority = 5;
	Source->Force = FlatDirection * (Distance / Duration);
	Source->Duration = Duration;
	Source->StrengthOverTime = nullptr;
	Source->Settings.SetFlag(ERootMotionSourceSettingsFlags::IgnoreZAccumulate);
	Source->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::ClampVelocity;
	Source->FinishVelocityParams.SetVelocity = FVector::ZeroVector;
	Source->FinishVelocityParams.ClampVelocity = 0.f;
	Movement->ApplyRootMotionSource(Source);
	return true;
}

bool UBH_CombatFeelLibrary::ApplyHitPushback(AActor* Attacker, AActor* Victim, EBH_ImpactTier Tier, bool bBlocked)
{
	ACharacter* VictimCharacter = Cast<ACharacter>(Victim);
	if (!VictimCharacter || !Attacker || Attacker == Victim || !VictimCharacter->HasAuthority())
	{
		return false;
	}
	const UBH_CombatFeelSettings* Settings = UBH_CombatFeelSettings::Get();
	const int32 Index = static_cast<int32>(Tier);
	if (!Settings->bEnablePushback || !Settings->PushbackDistanceByTier.IsValidIndex(Index) || !Settings->PushbackDurationByTier.IsValidIndex(Index))
	{
		return false;
	}

	// The break montage owns a broken victim; a dead one needs no nudge.
	if (const UAbilitySystemComponent* VictimASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Victim))
	{
		if (VictimASC->HasMatchingGameplayTag(TAG_State_Combat_PostureBroken) || VictimASC->HasMatchingGameplayTag(TAG_State_Combat_Dead))
		{
			return false;
		}
		if (VictimASC->HasAttributeSetForAttribute(UAH_AttributeSet::GetHealthAttribute())
			&& VictimASC->GetNumericAttribute(UAH_AttributeSet::GetHealthAttribute()) <= 0.f)
		{
			return false;
		}
	}

	const FGameplayTag AttackerStance = UBH_StanceComponent::GetStanceTagOf(Attacker);
	const float* StanceMultiplier = AttackerStance.IsValid() ? Settings->PushbackStanceMultipliers.Find(AttackerStance) : nullptr;
	const float Distance = Settings->PushbackDistanceByTier[Index]
		* (StanceMultiplier ? *StanceMultiplier : Settings->DefaultPushbackStanceMultiplier)
		* (bBlocked ? Settings->BlockedPushbackFactor : 1.f);
	const float Duration = FMath::Max(Settings->PushbackDurationByTier[Index], 0.02f);
	const FVector Direction = (Victim->GetActorLocation() - Attacker->GetActorLocation()).GetSafeNormal2D();

	static uint16 GPushbackId = 0;
	GPushbackId = (GPushbackId == TNumericLimits<uint16>::Max()) ? 1 : GPushbackId + 1; // 0 stays unused
	const uint16 Id = GPushbackId;

	if (!ApplyPushbackSource(VictimCharacter, Direction, Distance, Duration, Id))
	{
		return false;
	}

	// A remote client victim predicts its own movement: give its owner the identical source. A locally controlled (host) or
	// server-owned (AI) victim needs nothing extra: the server's source IS the prediction / is replicated to simulated proxies.
	bool bSentToOwner = false;
	if (Settings->bPredictPushbackOnOwningClient && VictimCharacter->GetRemoteRole() == ROLE_AutonomousProxy)
	{
		if (ABH_CharacterBase* BHVictim = Cast<ABH_CharacterBase>(VictimCharacter))
		{
			BHVictim->Client_ApplyHitPushback(Direction, Distance, Duration, Id);
			bSentToOwner = true;
		}
	}

	UE_LOG(LogBHFeel, Verbose, TEXT("Pushback: attacker=%s victim=%s tier=%s blocked=%d stance=%s dist=%.0f dur=%.2f id=%u clientRPC=%d"),
		*GetNameSafe(Attacker), *GetNameSafe(Victim), TierName(Tier), bBlocked ? 1 : 0, *AttackerStance.ToString(), Distance, Duration, static_cast<uint32>(Id), bSentToOwner ? 1 : 0);
	return true;
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
