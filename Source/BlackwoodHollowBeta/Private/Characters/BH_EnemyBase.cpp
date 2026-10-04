// Blackwood Hollow - Native enemy / combat target base (implementation)

#include "Characters/BH_EnemyBase.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystem/BH_CombatFunctionLibrary.h"
#include "Combat/BH_WeaponLoadoutDataAsset.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/MeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "TimerManager.h"

ABH_EnemyBase::ABH_EnemyBase()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	DisplayName = NSLOCTEXT("BlackwoodHollow", "EnemyDefaultName", "Enemy");
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	// AI-controlled: GEs only need to replicate minimally; tags/cues/attributes still replicate.
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);

	// Default-subobject attribute sets are registered with the ASC automatically.
	AttributeSet = CreateDefaultSubobject<UAH_AttributeSet>(TEXT("AttributeSet"));

	if (USkeletalMeshComponent* SkelMesh = GetMesh())
	{
		// Anim notifies (hitboxes, combo windows) must fire even when not rendered (dedicated server / off-screen).
		SkelMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	}
}

void ABH_EnemyBase::InitAbilitySystem()
{
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->InitAbilityActorInfo(this, this);
	}
}

void ABH_EnemyBase::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	InitAbilitySystem();

	// Keep the AI controller on the same team (AI perception reads the controller's team).
	if (IGenericTeamAgentInterface* ControllerAgent = Cast<IGenericTeamAgentInterface>(NewController))
	{
		ControllerAgent->SetGenericTeamId(GetGenericTeamId());
	}
}

void ABH_EnemyBase::BeginPlay()
{
	Super::BeginPlay();

	InitAbilitySystem();

	if (HasAuthority() && AbilitySystemComponent && !bAbilitiesGranted)
	{
		for (const TSubclassOf<UGameplayAbility>& AbilityClass : DefaultAbilities)
		{
			if (AbilityClass && !AbilitySystemComponent->FindAbilitySpecFromClass(AbilityClass))
			{
				AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec(AbilityClass, 1, INDEX_NONE, this));
			}
		}
		bAbilitiesGranted = true;
	}

	// Passive posture / stamina regeneration (authority only; no-op if already applied).
	UBH_CombatFunctionLibrary::ApplyPassiveRegenEffects(this);

	if (AttributeSet && HasAuthority())
	{
		AttributeSet->OnHealthZero.AddUObject(this, &ABH_EnemyBase::HandleHealthZero);
	}

	// Cosmetic weapon meshes: every machine attaches its own copy.
	if (WeaponLoadouts)
	{
		TArray<UMeshComponent*> Attached;
		UBH_CombatFunctionLibrary::EquipWeaponsForStance(this, WeaponLoadouts, BH_Stance::FromLegacyName(FName(*WeaponLoadoutName)), Attached);
	}

	if (HasAuthority())
	{
		UWorld* World = GetWorld();
		if (World && bAutoAttack && AutoAttackAbility)
		{
			World->GetTimerManager().SetTimer(AutoAttackTimerHandle, this, &ABH_EnemyBase::DoAutoAttack, AutoAttackInterval, true, AutoAttackInterval);
		}
		if (World && bHoldBlock && BlockAbility)
		{
			// Polls so the guard is re-raised whenever it drops (e.g. after a posture break).
			World->GetTimerManager().SetTimer(HoldBlockTimerHandle, this, &ABH_EnemyBase::StartHoldBlock, 0.5f, true);
		}
	}
}

void ABH_EnemyBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearAllTimersForObject(this);
	}
	Super::EndPlay(EndPlayReason);
}

void ABH_EnemyBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bFaceTarget || !HasAuthority() || !AbilitySystemComponent)
	{
		return;
	}
	// No turning while broken / dead.
	if (AbilitySystemComponent->HasMatchingGameplayTag(TAG_State_Combat_PostureBroken)
		|| AbilitySystemComponent->HasMatchingGameplayTag(TAG_State_Combat_Dead))
	{
		return;
	}

	const APawn* Target = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Target || Target == this)
	{
		return;
	}

	const FVector ToTarget = (Target->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
	if (ToTarget.IsNearlyZero())
	{
		return;
	}

	const FRotator Desired(0.f, ToTarget.Rotation().Yaw, 0.f);
	SetActorRotation(FMath::RInterpTo(GetActorRotation(), Desired, DeltaSeconds, FaceTargetInterpSpeed));
}

float ABH_EnemyBase::GetHealth() const
{
	return AttributeSet ? AttributeSet->GetHealth() : 0.f;
}

float ABH_EnemyBase::GetPosture() const
{
	return AttributeSet ? AttributeSet->GetPosture() : 0.f;
}

void ABH_EnemyBase::DoAutoAttack()
{
	if (AbilitySystemComponent && AutoAttackAbility)
	{
		UBH_CombatFunctionLibrary::HandleMeleeAttackInput(this, AutoAttackAbility);
	}
}

void ABH_EnemyBase::StartHoldBlock()
{
	if (!AbilitySystemComponent || !BlockAbility)
	{
		return;
	}
	const FGameplayAbilitySpec* Spec = AbilitySystemComponent->FindAbilitySpecFromClass(BlockAbility);
	if (Spec && !Spec->IsActive())
	{
		AbilitySystemComponent->TryActivateAbility(Spec->Handle);
	}
}

void ABH_EnemyBase::HandleHealthZero(AActor* Killer)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutoAttackTimerHandle);
		if (bResetOnDeath)
		{
			World->GetTimerManager().SetTimer(ResetTimerHandle, this, &ABH_EnemyBase::ResetAfterDeath, ResetDelay, false);
		}
	}

	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->CancelAllAbilities();
	}

	K2_OnDeath(Killer);
}

void ABH_EnemyBase::ResetAfterDeath()
{
	if (!AbilitySystemComponent || !AttributeSet)
	{
		return;
	}

	AbilitySystemComponent->SetNumericAttributeBase(UAH_AttributeSet::GetHealthAttribute(), AttributeSet->GetMaxHealth());
	AbilitySystemComponent->SetNumericAttributeBase(UAH_AttributeSet::GetPostureAttribute(), AttributeSet->GetMaxPosture());
	AbilitySystemComponent->SetLooseGameplayTagCount(TAG_State_Combat_Dead, 0);
	AbilitySystemComponent->SetLooseGameplayTagCount(TAG_State_Combat_PostureBroken, 0);

	UWorld* World = GetWorld();
	if (World && bAutoAttack && AutoAttackAbility)
	{
		World->GetTimerManager().SetTimer(AutoAttackTimerHandle, this, &ABH_EnemyBase::DoAutoAttack, AutoAttackInterval, true, AutoAttackInterval);
	}
}
