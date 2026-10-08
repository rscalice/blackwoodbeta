// Blackwood Hollow - player progression (XP / level) component (implementation)

#include "Progression/BH_ProgressionComponent.h"
#include "Progression/BH_RPGSettings.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "UI/BH_HUDSubsystem.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

UBH_ProgressionComponent::UBH_ProgressionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UBH_ProgressionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UBH_ProgressionComponent, CurrentXP);
	DOREPLIFETIME(UBH_ProgressionComponent, Level);
}

UBH_ProgressionComponent* UBH_ProgressionComponent::FindProgression(const AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}
	const APlayerState* PlayerState = Cast<APlayerState>(Actor);
	if (!PlayerState)
	{
		if (const APawn* Pawn = Cast<APawn>(Actor))
		{
			PlayerState = Pawn->GetPlayerState();
		}
		else if (const AController* Controller = Cast<AController>(Actor))
		{
			PlayerState = Controller->PlayerState;
		}
	}
	return PlayerState ? PlayerState->FindComponentByClass<UBH_ProgressionComponent>() : nullptr;
}

// ----------------------------------------------------------------------------
// Lifecycle
// ----------------------------------------------------------------------------

void UBH_ProgressionComponent::BeginPlay()
{
	Super::BeginPlay();

	APlayerState* PlayerState = Cast<APlayerState>(GetOwner());
	if (PlayerState && PlayerState->HasAuthority())
	{
		PlayerState->OnPawnSet.AddUniqueDynamic(this, &UBH_ProgressionComponent::HandlePawnSet);
		bBoundPawnSet = true;

		// A pawn that was possessed before this component began play.
		if (APawn* ExistingPawn = PlayerState->GetPawn())
		{
			ApplyToPawn(ExistingPawn);
		}
	}
}

void UBH_ProgressionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bBoundPawnSet)
	{
		if (APlayerState* PlayerState = Cast<APlayerState>(GetOwner()))
		{
			PlayerState->OnPawnSet.RemoveDynamic(this, &UBH_ProgressionComponent::HandlePawnSet);
		}
		bBoundPawnSet = false;
	}
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RetryTimer);
	}
	Super::EndPlay(EndPlayReason);
}

void UBH_ProgressionComponent::HandlePawnSet(APlayerState* Player, APawn* NewPawn, APawn* OldPawn)
{
	if (NewPawn)
	{
		ApplyToPawn(NewPawn);
	}
}

// ----------------------------------------------------------------------------
// State
// ----------------------------------------------------------------------------

int32 UBH_ProgressionComponent::GetXPToNextLevel() const
{
	const UBH_RPGSettings* Settings = UBH_RPGSettings::Get();
	return Settings ? Settings->GetXPToNextLevel(Level) : 0;
}

float UBH_ProgressionComponent::GetXPProgress01() const
{
	const int32 Needed = GetXPToNextLevel();
	return Needed > 0 ? FMath::Clamp(static_cast<float>(CurrentXP) / static_cast<float>(Needed), 0.f, 1.f) : 1.f;
}

void UBH_ProgressionComponent::AddXP(int32 Amount)
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || Amount <= 0)
	{
		return;
	}

	const UBH_RPGSettings* Settings = UBH_RPGSettings::Get();
	if (!Settings)
	{
		return;
	}

	const int32 MaxLevel = UBH_RPGSettings::GetMaxLevel();
	const int32 OldLevel = Level;

	if (Level < MaxLevel)
	{
		CurrentXP += Amount;
		while (Level < MaxLevel)
		{
			const int32 Needed = Settings->GetXPToNextLevel(Level);
			if (Needed <= 0 || CurrentXP < Needed)
			{
				break;
			}
			CurrentXP -= Needed;
			++Level;
		}
		if (Level >= MaxLevel)
		{
			CurrentXP = 0; // at the cap there is nothing to bank
		}
	}

	if (Level != OldLevel)
	{
		if (const APlayerState* PlayerState = Cast<APlayerState>(GetOwner()))
		{
			if (APawn* Pawn = PlayerState->GetPawn())
			{
				ApplyToPawn(Pawn);
			}
		}
		NotifyLevelUp(OldLevel, Level);
	}
	BroadcastXPChanged();
	GetOwner()->ForceNetUpdate();
}

void UBH_ProgressionComponent::SetLevel(int32 NewLevel)
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	const int32 Clamped = FMath::Clamp(NewLevel, 1, UBH_RPGSettings::GetMaxLevel());
	const int32 OldLevel = Level;
	Level = Clamped;
	CurrentXP = 0;

	if (const APlayerState* PlayerState = Cast<APlayerState>(GetOwner()))
	{
		if (APawn* Pawn = PlayerState->GetPawn())
		{
			ApplyToPawn(Pawn);
		}
	}
	if (Level > OldLevel)
	{
		NotifyLevelUp(OldLevel, Level);
	}
	BroadcastXPChanged();
	GetOwner()->ForceNetUpdate();
}

// ----------------------------------------------------------------------------
// Applying the level to the pawn (server)
// ----------------------------------------------------------------------------

UAbilitySystemComponent* UBH_ProgressionComponent::FindPawnASC(const APawn* Pawn) const
{
	if (!Pawn)
	{
		return nullptr;
	}
	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Pawn);
	if (!ASC)
	{
		ASC = Pawn->FindComponentByClass<UAbilitySystemComponent>();
	}
	return ASC;
}

void UBH_ProgressionComponent::ApplyToPawn(APawn* Pawn)
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || !Pawn)
	{
		return;
	}

	UAbilitySystemComponent* ASC = FindPawnASC(Pawn);
	if (!ASC || !ASC->HasAttributeSetForAttribute(UAH_AttributeSet::GetLevelAttribute()))
	{
		// The pawn's combat setup (SetupCombatCharacter) has not created the attribute set yet: try again shortly.
		UWorld* World = GetWorld();
		if (World)
		{
			if (PendingPawn.Get() != Pawn)
			{
				RetryCount = 0;
			}
			PendingPawn = Pawn;
			if (RetryCount < MaxRetries)
			{
				World->GetTimerManager().SetTimer(RetryTimer, this, &UBH_ProgressionComponent::RetryApplyToPawn, RetryInterval, false);
			}
		}
		return;
	}

	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RetryTimer);
	}
	PendingPawn.Reset();
	RetryCount = 0;

	const UBH_RPGSettings* Settings = UBH_RPGSettings::Get();

	ASC->SetNumericAttributeBase(UAH_AttributeSet::GetLevelAttribute(), static_cast<float>(Level));

	// Max values first (the attribute set clamps the current value to its max), then the refill.
	float MaxHealth = 0.f;
	float MaxPosture = 0.f;
	float MaxStamina = 0.f;
	if (Settings)
	{
		if (Settings->GetPlayerStat(FName(BH_ScalingRows::MaxHealth), Level, MaxHealth))
		{
			ASC->SetNumericAttributeBase(UAH_AttributeSet::GetMaxHealthAttribute(), MaxHealth);
		}
		if (Settings->GetPlayerStat(FName(BH_ScalingRows::MaxPosture), Level, MaxPosture))
		{
			ASC->SetNumericAttributeBase(UAH_AttributeSet::GetMaxPostureAttribute(), MaxPosture);
		}
		if (Settings->GetPlayerStat(FName(BH_ScalingRows::MaxStamina), Level, MaxStamina))
		{
			ASC->SetNumericAttributeBase(UAH_AttributeSet::GetMaxStaminaAttribute(), MaxStamina);
		}
	}

	// Refill from the (possibly equipment-boosted) current max. A dead pawn keeps its zero health; the respawned pawn is refilled by its own apply.
	if (!ASC->HasMatchingGameplayTag(TAG_State_Combat_Dead))
	{
		const float CurrentMaxHealth = ASC->GetNumericAttribute(UAH_AttributeSet::GetMaxHealthAttribute());
		const float CurrentMaxPosture = ASC->GetNumericAttribute(UAH_AttributeSet::GetMaxPostureAttribute());
		const float CurrentMaxStamina = ASC->GetNumericAttribute(UAH_AttributeSet::GetMaxStaminaAttribute());
		ASC->SetNumericAttributeBase(UAH_AttributeSet::GetHealthAttribute(), CurrentMaxHealth);
		ASC->SetNumericAttributeBase(UAH_AttributeSet::GetPostureAttribute(), CurrentMaxPosture);
		ASC->SetNumericAttributeBase(UAH_AttributeSet::GetStaminaAttribute(), CurrentMaxStamina);
	}
}

void UBH_ProgressionComponent::RetryApplyToPawn()
{
	++RetryCount;
	if (APawn* Pawn = PendingPawn.Get())
	{
		ApplyToPawn(Pawn);
	}
}

// ----------------------------------------------------------------------------
// Events
// ----------------------------------------------------------------------------

void UBH_ProgressionComponent::BroadcastXPChanged()
{
	OnXPChanged.Broadcast(CurrentXP, GetXPToNextLevel());
}

FBH_LevelUpInfo UBH_ProgressionComponent::BuildLevelUpInfo(int32 FromLevel, int32 ToLevel)
{
	FBH_LevelUpInfo Info;
	Info.PreviousLevel = FromLevel;
	Info.NewLevel = ToLevel;

	const UBH_RPGSettings* Settings = UBH_RPGSettings::Get();
	if (!Settings)
	{
		return Info;
	}

	// Gain of one curve row between the two levels; a missing table / row (either level) is a gain of 0.
	const auto RowGain = [Settings, FromLevel, ToLevel](const TCHAR* RowName) -> float
	{
		float OldValue = 0.f;
		float NewValue = 0.f;
		if (Settings->GetPlayerStat(FName(RowName), FromLevel, OldValue) && Settings->GetPlayerStat(FName(RowName), ToLevel, NewValue))
		{
			return NewValue - OldValue;
		}
		return 0.f;
	};

	Info.HealthGain = RowGain(BH_ScalingRows::MaxHealth);
	Info.PostureGain = RowGain(BH_ScalingRows::MaxPosture);
	Info.StaminaGain = RowGain(BH_ScalingRows::MaxStamina);
	return Info;
}

void UBH_ProgressionComponent::NotifyLevelUp(int32 PreviousLevel, int32 NewLevel)
{
	// SERVER only (AddXP / SetLevel). This is the one place a real level up is announced: the respawn re-apply (ApplyToPawn) and the
	// initial replication never come through here.
	const FBH_LevelUpInfo Info = BuildLevelUpInfo(PreviousLevel, NewLevel);

	// Server side: listeners on the server get the events here; the owning client (or the listen-server host's own
	// local controller) gets them through the Client RPC, which also shows the flourish exactly once.
	OnLevelUp.Broadcast(NewLevel);
	OnLevelUpDetailed.Broadcast(Info);
	ExecuteLevelUpCue(NewLevel);
	ClientNotifyLevelUp(Info);
}

void UBH_ProgressionComponent::ExecuteLevelUpCue(int32 NewLevel) const
{
	const APlayerState* PlayerState = Cast<APlayerState>(GetOwner());
	APawn* Pawn = PlayerState ? PlayerState->GetPawn() : nullptr;
	UAbilitySystemComponent* ASC = FindPawnASC(Pawn);
	if (!ASC)
	{
		return; // no pawn right now: the stats and the HUD flourish still happen, only the world cue is skipped
	}

	// Replicated cosmetic cue (executed on the server, played on every machine): same pattern as the posture-break cue.
	FGameplayCueParameters CueParams;
	CueParams.Instigator = Pawn;
	CueParams.EffectCauser = Pawn;
	CueParams.SourceObject = Pawn;
	CueParams.Location = Pawn->GetActorLocation();
	CueParams.TargetAttachComponent = Pawn->GetRootComponent();
	CueParams.RawMagnitude = static_cast<float>(NewLevel);
	ASC->ExecuteGameplayCue(TAG_GameplayCue_Player_LevelUp, CueParams);
}

void UBH_ProgressionComponent::ClientNotifyLevelUp_Implementation(const FBH_LevelUpInfo& Info)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		// the server already broadcast its own copy
		OnLevelUp.Broadcast(Info.NewLevel);
		OnLevelUpDetailed.Broadcast(Info);
	}
	ShowLevelUpFlourish(Info);
}

void UBH_ProgressionComponent::ShowLevelUpFlourish(const FBH_LevelUpInfo& Info) const
{
	const APlayerState* PlayerState = Cast<APlayerState>(GetOwner());
	const APlayerController* PC = PlayerState ? PlayerState->GetPlayerController() : nullptr;
	if (!PC || !PC->IsLocalController())
	{
		return;
	}
	if (ULocalPlayer* LocalPlayer = PC->GetLocalPlayer())
	{
		if (UBH_HUDSubsystem* HUD = LocalPlayer->GetSubsystem<UBH_HUDSubsystem>())
		{
			HUD->ShowLevelUpDetailed(Info);
		}
	}
}

void UBH_ProgressionComponent::OnRep_CurrentXP()
{
	BroadcastXPChanged();
}

void UBH_ProgressionComponent::OnRep_Level()
{
	// A new level changes XPToNextLevel too. (The level-up event itself arrives through ClientNotifyLevelUp.)
	BroadcastXPChanged();
}

// ----------------------------------------------------------------------------
// Kill XP
// ----------------------------------------------------------------------------

void UBH_ProgressionComponent::GrantKillXP(const AActor* Source, int32 Amount)
{
	if (!Source || Amount <= 0 || !Source->HasAuthority())
	{
		return;
	}
	UWorld* World = Source->GetWorld();
	const UBH_RPGSettings* Settings = UBH_RPGSettings::Get();
	if (!World || !Settings)
	{
		return;
	}

	const float Radius = Settings->XPShareRadius;
	const double RadiusSq = static_cast<double>(Radius) * static_cast<double>(Radius);
	const FVector Origin = Source->GetActorLocation();

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (!Pawn)
		{
			continue;
		}

		// Living only: no Dead tag and Health above zero.
		const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Pawn);
		if (!ASC || ASC->HasMatchingGameplayTag(TAG_State_Combat_Dead)
			|| (ASC->HasAttributeSetForAttribute(UAH_AttributeSet::GetHealthAttribute()) && ASC->GetNumericAttribute(UAH_AttributeSet::GetHealthAttribute()) <= 0.f))
		{
			continue;
		}

		if (Radius > 0.f && FVector::DistSquared(Pawn->GetActorLocation(), Origin) > RadiusSq)
		{
			continue;
		}

		if (UBH_ProgressionComponent* Progression = FindProgression(PC))
		{
			Progression->AddXP(Amount);
		}
	}
}
