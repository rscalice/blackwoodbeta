// Blackwood Hollow - native base for the motion-matching character (implementation)

#include "Characters/BH_CharacterBase.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystem/BH_CombatFunctionLibrary.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "Combat/BH_StanceComponent.h"
#include "Combat/BH_CombatFeel.h"
#include "Characters/BH_StanceMovementProfile.h"
#include "AbilitySystemComponent.h"
#include "AIController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"

ABH_CharacterBase::ABH_CharacterBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	AttributeSet = CreateDefaultSubobject<UAH_AttributeSet>(TEXT("AttributeSet"));
	StanceComponent = CreateDefaultSubobject<UBH_StanceComponent>(TEXT("StanceComponent"));
}

void ABH_CharacterBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABH_CharacterBase, CombatTeam);
	DOREPLIFETIME(ABH_CharacterBase, bLockOnStrafe);
	DOREPLIFETIME(ABH_CharacterBase, AIDesiredGait);
}

void ABH_CharacterBase::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	if (AbilitySystemComponent && Cast<AAIController>(NewController))
	{
		AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);
	}
	InitAbilitySystem();

	if (CombatTeam != EBH_CombatTeam::Neutral)
	{
		if (IGenericTeamAgentInterface* ControllerAgent = Cast<IGenericTeamAgentInterface>(NewController))
		{
			ControllerAgent->SetGenericTeamId(GetGenericTeamId());
		}
	}
}

void ABH_CharacterBase::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	InitAbilitySystem();
}

void ABH_CharacterBase::BeginPlay()
{
	Super::BeginPlay();

	InitAbilitySystem();
	if (HasAuthority())
	{
		GrantDefaultAbilities();
		UBH_CombatFunctionLibrary::ApplyPassiveRegenEffects(this);
	}
}

void ABH_CharacterBase::InitAbilitySystem()
{
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->InitAbilityActorInfo(this, this);
	}
}

void ABH_CharacterBase::GrantDefaultAbilities()
{
	if (bAbilitiesGranted)
	{
		return;
	}
	UBH_CombatFunctionLibrary::GrantCombatAbilities(this, DefaultAbilities);
	bAbilitiesGranted = true;
}

void ABH_CharacterBase::SetLockOnStrafe(bool bEnabled)
{
	if (bLockOnStrafe == bEnabled)
	{
		return;
	}
	bLockOnStrafe = bEnabled;
	if (!HasAuthority())
	{
		ServerSetLockOnStrafe(bEnabled);
	}
}

void ABH_CharacterBase::ServerSetLockOnStrafe_Implementation(bool bEnabled)
{
	bLockOnStrafe = bEnabled;
}

void ABH_CharacterBase::OnRep_LockOnStrafe()
{
}

void ABH_CharacterBase::Client_ApplyHitPushback_Implementation(FVector Direction, float Distance, float Duration, uint16 Id)
{
	// Autonomous proxy only (the server never RPCs itself): same source as the server applied, so the next move agrees.
	if (!HasAuthority())
	{
		UBH_CombatFeelLibrary::ApplyPushbackSource(this, Direction, Distance, Duration, Id);
	}
}

EBH_RotationMode ABH_CharacterBase::GetRotationMode() const
{
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	return Movement && Movement->bUseControllerDesiredRotation ? EBH_RotationMode::Strafe : EBH_RotationMode::OrientToMovement;
}

void ABH_CharacterBase::SetGaitFromByte(uint8 GaitByte)
{
	CurrentGait = static_cast<EBH_Gait>(FMath::Clamp<int32>(GaitByte, 0, 2));
}

FGameplayTag ABH_CharacterBase::GetWeaponStance() const
{
	return StanceComponent ? StanceComponent->GetCurrentStance() : FGameplayTag();
}

UBH_StanceMovementProfile* ABH_CharacterBase::GetMovementProfile() const
{
	return StanceComponent ? StanceComponent->GetActiveMovementProfile() : nullptr;
}

void ABH_CharacterBase::SetAIDesiredGait(EBH_Gait NewGait)
{
	if (HasAuthority())
	{
		AIDesiredGait = NewGait;
	}
}

bool ABH_CharacterBase::IsCombatMovementLocked() const
{
	return AbilitySystemComponent && AbilitySystemComponent->HasMatchingGameplayTag(TAG_State_Combat_MovementLocked);
}

void ABH_CharacterBase::AddMovementInput(FVector WorldDirection, float ScaleValue, bool bForce)
{
	if (!bForce && IsCombatMovementLocked())
	{
		return;
	}
	Super::AddMovementInput(WorldDirection, ScaleValue, bForce);
}

FVector ABH_CharacterBase::GetGaitSpeedsOr(FVector Fallback) const
{
	if (IsCombatMovementLocked())
	{
		return FVector::ZeroVector;
	}
	const UBH_StanceMovementProfile* Profile = GetMovementProfile();
	return Profile ? Profile->GetGaitSettings(CurrentGait).Speeds : Fallback;
}

FVector ABH_CharacterBase::GetCrouchSpeedsOr(FVector Fallback) const
{
	const UBH_StanceMovementProfile* Profile = GetMovementProfile();
	return Profile ? Profile->CrouchSpeeds : Fallback;
}

float ABH_CharacterBase::GetMaxAccelerationOr(float Fallback) const
{
	if (IsCombatMovementLocked())
	{
		return 0.f;
	}
	const UBH_StanceMovementProfile* Profile = GetMovementProfile();
	return Profile ? Profile->GetMaxAccelerationFor(CurrentGait, GetVelocity().Size2D()) : Fallback;
}

float ABH_CharacterBase::GetGroundFrictionOr(float Fallback) const
{
	const UBH_StanceMovementProfile* Profile = GetMovementProfile();
	return Profile ? Profile->GetGroundFrictionFor(CurrentGait, GetVelocity().Size2D()) : Fallback;
}

float ABH_CharacterBase::ComputeBrakingDecelerationOr(bool bHasMovementInput, float Fallback)
{
	if (IsCombatMovementLocked())
	{
		BrakingBand.Reset();
		return CombatLockBrakingDeceleration;
	}
	const UBH_StanceMovementProfile* Profile = GetMovementProfile();
	if (!Profile)
	{
		BrakingBand.Reset();
		return Fallback;
	}
	if (bHasMovementInput)
	{
		BrakingBand.Reset();
		return Profile->BrakingDecelerationWithInput;
	}
	if (!BrakingBand.IsSet())
	{
		BrakingBand = Profile->GetBrakingBandForSpeed(GetVelocity().Size2D());
	}
	return Profile->GetGaitSettings(*BrakingBand).BrakingDecelerationNoInput;
}

FVector ABH_CharacterBase::GetGaitSpeeds(EBH_Gait Gait) const
{
	const UBH_StanceMovementProfile* Profile = GetMovementProfile();
	return Profile ? Profile->GetGaitSettings(Gait).Speeds : FVector::ZeroVector;
}

float ABH_CharacterBase::GetMaxAccelerationFor(EBH_Gait Gait, float Speed2D) const
{
	const UBH_StanceMovementProfile* Profile = GetMovementProfile();
	return Profile ? Profile->GetMaxAccelerationFor(Gait, Speed2D) : 0.f;
}

float ABH_CharacterBase::GetBrakingDeceleration(bool bHasMovementInput) const
{
	if (IsCombatMovementLocked())
	{
		return CombatLockBrakingDeceleration;
	}
	const UBH_StanceMovementProfile* Profile = GetMovementProfile();
	if (!Profile)
	{
		return 0.f;
	}
	if (bHasMovementInput)
	{
		return Profile->BrakingDecelerationWithInput;
	}
	const EBH_Gait Band = BrakingBand.IsSet() ? *BrakingBand : Profile->GetBrakingBandForSpeed(GetVelocity().Size2D());
	return Profile->GetGaitSettings(Band).BrakingDecelerationNoInput;
}
