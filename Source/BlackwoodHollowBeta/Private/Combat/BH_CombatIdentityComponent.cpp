// Blackwood Hollow - combat identity for non-ABH_EnemyBase characters (implementation)

#include "Combat/BH_CombatIdentityComponent.h"
#include "Combat/BH_CombatFeel.h"
#include "Combat/BH_StanceWatcherComponent.h"
#include "Combat/BH_StanceComponent.h"
#include "Combat/BH_EnemyRank.h"
#include "Characters/BH_EnemyResetComponent.h"
#include "Characters/BH_CharacterBase.h"
#include "Combat/BH_WeaponLoadoutDataAsset.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystem/BH_CombatFunctionLibrary.h"
#include "Progression/BH_ProgressionComponent.h"
#include "Loot/BH_LootLibrary.h"
#include "Quests/BH_QuestDirector.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "Abilities/GameplayAbility.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInterface.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "TimerManager.h"

UBH_CombatIdentityComponent::UBH_CombatIdentityComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	DisplayName = NSLOCTEXT("BlackwoodHollow", "IdentityDefaultName", "Enemy");
}

void UBH_CombatIdentityComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UBH_CombatIdentityComponent, DisplayName);
	DOREPLIFETIME(UBH_CombatIdentityComponent, CombatTeam);
	DOREPLIFETIME(UBH_CombatIdentityComponent, AggroTarget);
	DOREPLIFETIME(UBH_CombatIdentityComponent, bDead);
	DOREPLIFETIME(UBH_CombatIdentityComponent, Rank);
	DOREPLIFETIME(UBH_CombatIdentityComponent, EnemyLevel);
	DOREPLIFETIME(UBH_CombatIdentityComponent, Archetype);
}

namespace BH_CombatIdentityPrivate
{
	/** A parked bh.Enemy.Spawn / wave-spawner request (see SetPendingSpawn). One at a time: set and consumed inside one FinishSpawning call. */
	struct FPendingSpawn
	{
		TWeakObjectPtr<const AActor> Actor;
		int32 RankOverride = -1;
		int32 LevelOverride = 0;
	};
	static FPendingSpawn GPendingSpawn;
}

void UBH_CombatIdentityComponent::SetPendingSpawn(const AActor* Actor, int32 RankOverride, int32 LevelOverride)
{
	BH_CombatIdentityPrivate::GPendingSpawn.Actor = Actor;
	BH_CombatIdentityPrivate::GPendingSpawn.RankOverride = RankOverride;
	BH_CombatIdentityPrivate::GPendingSpawn.LevelOverride = LevelOverride;
}

int32 UBH_CombatIdentityComponent::GetXPReward() const
{
	return UBH_EnemyRankLibrary::ComputeXPReward(ScalingRowPrefix, EnemyLevel, XPReward, Rank);
}

FBH_OnAnyAggroTargetChanged& UBH_CombatIdentityComponent::OnAnyAggroTargetChanged()
{
	static FBH_OnAnyAggroTargetChanged Delegate;
	return Delegate;
}

void UBH_CombatIdentityComponent::SetAggroTarget(AActor* NewTarget)
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || AggroTarget == NewTarget)
	{
		return;
	}
	AggroTarget = NewTarget;
	// The server (and a listen-server host) never gets the OnRep: notify here. Clients notify from OnRep_AggroTarget.
	OnAggroTargetChanged.Broadcast(AggroTarget);
	OnAnyAggroTargetChanged().Broadcast(this, AggroTarget);
}

void UBH_CombatIdentityComponent::OnRep_AggroTarget()
{
	OnAggroTargetChanged.Broadcast(AggroTarget);
	OnAnyAggroTargetChanged().Broadcast(this, AggroTarget);
}

UBH_CombatIdentityComponent* UBH_CombatIdentityComponent::Find(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UBH_CombatIdentityComponent>() : nullptr;
}

float UBH_CombatIdentityComponent::GetOutgoingCombatMultiplier(const AActor* Actor)
{
	const UBH_CombatIdentityComponent* Identity = Find(Actor);
	const float Base = Identity ? Identity->OutgoingCombatMultiplier : 1.f;
	return Base * UBH_EnemyRankLibrary::GetDamageMultiplier(Actor);
}

UAbilitySystemComponent* UBH_CombatIdentityComponent::ResolveASC() const
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}
	if (const IAbilitySystemInterface* Interface = Cast<IAbilitySystemInterface>(Owner))
	{
		if (UAbilitySystemComponent* ASC = Interface->GetAbilitySystemComponent())
		{
			return ASC;
		}
	}
	return Owner->FindComponentByClass<UAbilitySystemComponent>();
}

void UBH_CombatIdentityComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	// The stance watcher resolves loadouts from an owner variable or its own fallback: feed it ours (every machine).
	if (UBH_StanceWatcherComponent* Watcher = UBH_StanceWatcherComponent::FindStanceWatcher(Owner))
	{
		if (WeaponLoadouts && !Watcher->FallbackLoadouts)
		{
			Watcher->FallbackLoadouts = WeaponLoadouts;
		}
	}

	// ABH_CharacterBase descendants (motion-matching enemies): the replicated base team mirrors ours, and the native
	// stance component takes our loadouts when it has none of its own.
	if (Owner->HasAuthority())
	{
		if (ABH_CharacterBase* BaseCharacter = Cast<ABH_CharacterBase>(Owner))
		{
			BaseCharacter->SetGenericTeamId(BH_CombatTeam::ToGenericTeamId(CombatTeam));
		}
	}
	if (UBH_StanceComponent* NativeStance = UBH_StanceComponent::FindStanceComponent(Owner))
	{
		if (WeaponLoadouts && !NativeStance->WeaponLoadouts)
		{
			NativeStance->WeaponLoadouts = WeaponLoadouts;
		}
	}

	ApplyCosmetics();
	ApplyCollision();
	// GASP re-initialises its meshes shortly after possession; re-apply once so late changes are covered.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(CosmeticsTimer, this, &UBH_CombatIdentityComponent::ApplyLateSetup, 0.6f, false);
	}

	if (Owner->HasAuthority())
	{
		// A parked spawn request for THIS actor (bh.Enemy.Spawn / wave spawner) overrides the Blueprint's rank / level.
		BH_CombatIdentityPrivate::FPendingSpawn& Pending = BH_CombatIdentityPrivate::GPendingSpawn;
		if (Pending.Actor.Get() == Owner)
		{
			if (Pending.RankOverride >= 0)
			{
				Rank = static_cast<EBH_EnemyRank>(FMath::Clamp(Pending.RankOverride, 0, 3));
			}
			if (Pending.LevelOverride > 0)
			{
				EnemyLevel = Pending.LevelOverride;
			}
			Pending = BH_CombatIdentityPrivate::FPendingSpawn();
		}
		InitServer();
		// Phase 11F: remembers the spawn transform and resets this enemy on a party wipe (no Blueprint edit needed).
		UBH_EnemyResetComponent::EnsureOn(Owner);
	}

	// An actor that arrives already dead (late join, relevancy) ragdolls right away; the OnRep waits for BeginPlay.
	if (bDead)
	{
		SyncRagdollToDeadState(Owner->GetVelocity());
	}
}

void UBH_CombatIdentityComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(StanceTimer);
		World->GetTimerManager().ClearTimer(CosmeticsTimer);
	}
	if (HealthZeroHandle.IsValid())
	{
		if (UAbilitySystemComponent* ASC = ResolveASC())
		{
			if (UAH_AttributeSet* Set = const_cast<UAH_AttributeSet*>(ASC->GetSet<UAH_AttributeSet>()))
			{
				Set->OnHealthZero.Remove(HealthZeroHandle);
			}
		}
		HealthZeroHandle.Reset();
	}
	Super::EndPlay(EndPlayReason);
}

void UBH_CombatIdentityComponent::ApplyLateSetup()
{
	ApplyCosmetics();
	ApplyCollision();
}

void UBH_CombatIdentityComponent::ApplyCollision()
{
	// Explicit both ways: SetupCombatCharacter (server) also blocks pawns, so bBlockPawns = false must undo that.
	if (AActor* Owner = GetOwner())
	{
		if (UCapsuleComponent* Capsule = Owner->FindComponentByClass<UCapsuleComponent>())
		{
			Capsule->SetCollisionResponseToChannel(ECC_Pawn, bBlockPawns ? ECR_Block : ECR_Ignore);
		}
	}
}

void UBH_CombatIdentityComponent::ApplyInitialStats(UAbilitySystemComponent* ASC) const
{
	if (!ASC || !ASC->GetSet<UAH_AttributeSet>())
	{
		return;
	}
	// Max values first, then the current value follows its max.
	if (bOverrideMaxHealth)
	{
		ASC->SetNumericAttributeBase(UAH_AttributeSet::GetMaxHealthAttribute(), MaxHealth);
		ASC->SetNumericAttributeBase(UAH_AttributeSet::GetHealthAttribute(), MaxHealth);
	}
	if (bOverrideMaxPosture)
	{
		ASC->SetNumericAttributeBase(UAH_AttributeSet::GetMaxPostureAttribute(), MaxPosture);
		ASC->SetNumericAttributeBase(UAH_AttributeSet::GetPostureAttribute(), MaxPosture);
	}
	if (bOverrideMaxStamina)
	{
		ASC->SetNumericAttributeBase(UAH_AttributeSet::GetMaxStaminaAttribute(), MaxStamina);
		ASC->SetNumericAttributeBase(UAH_AttributeSet::GetStaminaAttribute(), MaxStamina);
	}
	if (bOverrideAttackPower)
	{
		ASC->SetNumericAttributeBase(UAH_AttributeSet::GetAttackPowerAttribute(), AttackPower);
	}
	if (bOverrideDefense)
	{
		ASC->SetNumericAttributeBase(UAH_AttributeSet::GetDefenseAttribute(), Defense);
	}
	if (bOverrideAttackSpeed)
	{
		ASC->SetNumericAttributeBase(UAH_AttributeSet::GetAttackSpeedAttribute(), AttackSpeed);
	}
}

void UBH_CombatIdentityComponent::ApplyCosmetics()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	TArray<USkeletalMeshComponent*> Meshes;
	Owner->GetComponents<USkeletalMeshComponent>(Meshes);
	for (USkeletalMeshComponent* Mesh : Meshes)
	{
		if (!Mesh || Mesh->ComponentHasTag(FName(TEXT("BH.Weapon"))))
		{
			continue;
		}
		// Anim notifies (hitboxes, combo windows) must fire even when the mesh is not rendered.
		Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		if (OverlayMaterial)
		{
			Mesh->SetOverlayMaterial(OverlayMaterial);
		}
	}
	// Warm the hit-flash shader after the identity overlay is in place (it is restored next tick). Local, cosmetic.
	UBH_CombatFeelLibrary::WarmHitFlash(Owner);
}

void UBH_CombatIdentityComponent::InitServer()
{
	AActor* Owner = GetOwner();
	UAbilitySystemComponent* ASC = ResolveASC();
	if (!Owner || !ASC)
	{
		UE_LOG(LogBHCombat, Warning, TEXT("CombatIdentity: no AbilitySystemComponent on '%s'."), *GetNameSafe(Owner));
		return;
	}

	// AI-controlled: GEs only need to replicate minimally; tags / attributes still replicate.
	ASC->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);

	// A null OverloadBurst class is safe (SetupCombatCharacter only grants it when set).
	UBH_CombatFunctionLibrary::SetupCombatCharacter(Owner, nullptr);
	ApplyInitialStats(ASC);
	// Phase 12G: level curve rows (when a prefix is set), then the rank multipliers on top.
	UBH_EnemyRankLibrary::ApplyLevelScalingToASC(ASC, ScalingRowPrefix, EnemyLevel);
	UBH_EnemyRankLibrary::ApplyRankToStats(ASC, Rank);
	ApplyCollision();
	UBH_CombatFunctionLibrary::GrantCombatAbilities(Owner, DefaultAbilities);

	if (UAH_AttributeSet* Set = const_cast<UAH_AttributeSet*>(ASC->GetSet<UAH_AttributeSet>()))
	{
		HealthZeroHandle = Set->OnHealthZero.AddUObject(this, &UBH_CombatIdentityComponent::HandleHealthZero);
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(StanceTimer, this, &UBH_CombatIdentityComponent::ApplyStartingStance, FMath::Max(0.01f, StartingStanceDelay), false);
	}
}

void UBH_CombatIdentityComponent::ApplyStartingStance()
{
	if (AActor* Owner = GetOwner())
	{
		if (StartingStanceTag.IsValid())
		{
			if (UBH_StanceComponent* NativeStance = UBH_StanceComponent::FindStanceComponent(Owner))
			{
				NativeStance->SetStance(StartingStanceTag);
				return;
			}
		}
		if (!StartingStance.IsNone())
		{
			UBH_CombatFunctionLibrary::ApplyOverlayPoseByDisplayName(Owner, StartingStance.ToString());
		}
	}
}

void UBH_CombatIdentityComponent::OnRep_Dead()
{
	if (!HasBegunPlay())
	{
		return; // BeginPlay applies the initial state
	}
	const AActor* OwnerActor = GetOwner();
	SyncRagdollToDeadState(OwnerActor ? OwnerActor->GetVelocity() : FVector::ZeroVector);
}

void UBH_CombatIdentityComponent::SyncRagdollToDeadState(const FVector& InheritVelocity)
{
	ABH_CharacterBase* BaseCharacter = Cast<ABH_CharacterBase>(GetOwner());
	if (!BaseCharacter)
	{
		return;
	}
	if (bDead && !BaseCharacter->IsRagdollActive())
	{
		BaseCharacter->StartRagdollLocal(InheritVelocity);
	}
	else if (!bDead && BaseCharacter->IsRagdollActive())
	{
		BaseCharacter->StopRagdollLocal();
		if (UCharacterMovementComponent* MoveComp = BaseCharacter->GetCharacterMovement())
		{
			MoveComp->Velocity = FVector::ZeroVector;
			MoveComp->SetMovementMode(MOVE_Falling); // lands on the floor next tick
		}
	}
}

void UBH_CombatIdentityComponent::HandleHealthZero(AActor* Killer)
{
	// The attribute set adds the loose State.Combat.Dead tag right after this broadcast.
	AActor* OwnerActor = GetOwner();
	const bool bWasDead = bDead;
	const FVector DeathVelocity = OwnerActor ? OwnerActor->GetVelocity() : FVector::ZeroVector; // before the ragdoll stops the CMC
	bDead = true;
	if (OwnerActor && !bWasDead)
	{
		OwnerActor->ForceNetUpdate();
	}

	if (UAbilitySystemComponent* ASC = ResolveASC())
	{
		ASC->CancelAllAbilities();
	}

	if (!bWasDead)
	{
		UBH_ProgressionComponent::GrantKillXP(OwnerActor, GetXPReward()); // positions are read now, before the body ragdolls / despawns
		UBH_LootLibrary::GrantEnemyDrops(OwnerActor, DropTable); // Phase 11C
		UBH_QuestDirector::ReportEnemyKill(OwnerActor); // Phase 14A-1: Kill(<identity>) for the party's quests

		// The server (and a listen-server host) gets no RepNotify: ragdoll here; clients do it in OnRep_Dead.
		SyncRagdollToDeadState(DeathVelocity);

		if (bDespawnOnDeath && OwnerActor)
		{
			if (APawn* PawnOwner = Cast<APawn>(OwnerActor))
			{
				PawnOwner->DetachFromControllerPendingDestroy(); // stop the AI
			}
			OwnerActor->SetLifeSpan(FMath::Max(0.1f, DespawnDelay));
		}
	}
	OnDeath.Broadcast(Killer);
}
