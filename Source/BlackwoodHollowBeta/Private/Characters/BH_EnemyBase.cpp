// Blackwood Hollow - Native enemy / combat target base (implementation)

#include "Characters/BH_EnemyBase.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystem/BH_CombatFunctionLibrary.h"
#include "Progression/BH_ProgressionComponent.h"
#include "Progression/BH_RPGSettings.h"
#include "Combat/BH_WeaponLoadoutDataAsset.h"
#include "Combat/BH_CombatIdentityComponent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Abilities/GameplayAbility.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/MeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

namespace BH_EnemyBasePrivate
{
	/** Living = no Dead tag and Health above zero (same test as UBH_ProgressionComponent::GrantKillXP). */
	static bool IsLiving(const AActor* Actor)
	{
		const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor);
		if (!ASC || ASC->HasMatchingGameplayTag(TAG_State_Combat_Dead))
		{
			return false;
		}
		return !ASC->HasAttributeSetForAttribute(UAH_AttributeSet::GetHealthAttribute())
			|| ASC->GetNumericAttribute(UAH_AttributeSet::GetHealthAttribute()) > 0.f;
	}
}

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
		// Attack telegraph decals must not tint the character itself.
		SkelMesh->SetReceivesDecals(false);
	}
}

void ABH_EnemyBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABH_EnemyBase, bDead);
	DOREPLIFETIME(ABH_EnemyBase, CombatTeam);
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

	// After the Initial* values / identity setup: the scaling table has the last word.
	if (HasAuthority())
	{
		InitializeServerStats();
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

	const AActor* Target = ResolveFaceTarget();
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

const AActor* ABH_EnemyBase::ResolveFaceTarget()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	const double Now = World->GetTimeSeconds();
	if (Now < NextFaceTargetSearchTime)
	{
		return CachedFaceTarget.Get();
	}
	NextFaceTargetSearchTime = Now + FaceTargetSearchInterval;

	// The AI's current aggro target wins while it is alive.
	if (const UBH_CombatIdentityComponent* Identity = FindComponentByClass<UBH_CombatIdentityComponent>())
	{
		const AActor* Aggro = Identity->GetAggroTarget();
		if (Aggro && Aggro != this && BH_EnemyBasePrivate::IsLiving(Aggro))
		{
			CachedFaceTarget = Aggro;
			return Aggro;
		}
	}

	// Otherwise the nearest living player pawn.
	const AActor* Best = nullptr;
	double BestDistSq = TNumericLimits<double>::Max();
	const FVector Origin = GetActorLocation();
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (!Pawn || Pawn == this || !BH_EnemyBasePrivate::IsLiving(Pawn))
		{
			continue;
		}
		const double DistSq = FVector::DistSquared(Pawn->GetActorLocation(), Origin);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = Pawn;
		}
	}

	CachedFaceTarget = Best;
	return Best;
}

void ABH_EnemyBase::InitializeServerStats()
{
	ApplyLevelScaling();
}

void ABH_EnemyBase::ApplyLevelScaling()
{
	if (!HasAuthority() || !AbilitySystemComponent || !AttributeSet)
	{
		return;
	}

	const int32 Lvl = FMath::Max(1, EnemyLevel);
	AbilitySystemComponent->SetNumericAttributeBase(UAH_AttributeSet::GetLevelAttribute(), static_cast<float>(Lvl));

	const UBH_RPGSettings* Settings = UBH_RPGSettings::Get();
	if (!Settings || ScalingRowPrefix.IsNone())
	{
		return;
	}

	float Value = 0.f;
	// Max first, then the current value (the attribute set clamps current to max).
	if (Settings->GetEnemyStat(ScalingRowPrefix, FName(BH_ScalingRows::MaxHealth), Lvl, Value))
	{
		AbilitySystemComponent->SetNumericAttributeBase(UAH_AttributeSet::GetMaxHealthAttribute(), Value);
		AbilitySystemComponent->SetNumericAttributeBase(UAH_AttributeSet::GetHealthAttribute(), Value);
	}
	if (Settings->GetEnemyStat(ScalingRowPrefix, FName(BH_ScalingRows::MaxPosture), Lvl, Value))
	{
		AbilitySystemComponent->SetNumericAttributeBase(UAH_AttributeSet::GetMaxPostureAttribute(), Value);
		AbilitySystemComponent->SetNumericAttributeBase(UAH_AttributeSet::GetPostureAttribute(), Value);
	}
	if (Settings->GetEnemyStat(ScalingRowPrefix, FName(BH_ScalingRows::AttackPower), Lvl, Value))
	{
		AbilitySystemComponent->SetNumericAttributeBase(UAH_AttributeSet::GetAttackPowerAttribute(), Value);
	}
	if (Settings->GetEnemyStat(ScalingRowPrefix, FName(BH_ScalingRows::Defense), Lvl, Value))
	{
		AbilitySystemComponent->SetNumericAttributeBase(UAH_AttributeSet::GetDefenseAttribute(), Value);
	}
}

int32 ABH_EnemyBase::GetXPReward() const
{
	float Value = 0.f;
	const UBH_RPGSettings* Settings = UBH_RPGSettings::Get();
	if (Settings && Settings->GetEnemyStat(ScalingRowPrefix, FName(BH_ScalingRows::XPReward), FMath::Max(1, EnemyLevel), Value))
	{
		return FMath::Max(0, FMath::RoundToInt(Value));
	}
	return FMath::Max(0, XPRewardOverride);
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
	if (bDead)
	{
		return; // the attribute set can report zero more than once: one death, one XP grant
	}
	bDead = true;

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

	// XP first (positions are read now, before a subclass ragdolls / despawns the body).
	UBH_ProgressionComponent::GrantKillXP(this, GetXPReward());

	OnDeathNative(Killer);
	K2_OnDeath(Killer);
	OnEnemyDeath.Broadcast(this, Killer);
}

void ABH_EnemyBase::ResetAfterDeath()
{
	if (!AbilitySystemComponent || !AttributeSet)
	{
		return;
	}

	AbilitySystemComponent->SetNumericAttributeBase(UAH_AttributeSet::GetHealthAttribute(), AttributeSet->GetMaxHealth());
	AbilitySystemComponent->SetNumericAttributeBase(UAH_AttributeSet::GetPostureAttribute(), AttributeSet->GetMaxPosture());
	AbilitySystemComponent->SetLooseGameplayTagCount(TAG_State_Combat_Dead, 0, EGameplayTagReplicationState::TagOnly);
	AbilitySystemComponent->SetLooseGameplayTagCount(TAG_State_Combat_PostureBroken, 0, EGameplayTagReplicationState::TagOnly);
	bDead = false;

	UWorld* World = GetWorld();
	if (World && bAutoAttack && AutoAttackAbility)
	{
		World->GetTimerManager().SetTimer(AutoAttackTimerHandle, this, &ABH_EnemyBase::DoAutoAttack, AutoAttackInterval, true, AutoAttackInterval);
	}
}
