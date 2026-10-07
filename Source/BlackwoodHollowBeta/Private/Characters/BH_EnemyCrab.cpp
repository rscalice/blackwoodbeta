// Blackwood Hollow - crab enemy (implementation)

#include "Characters/BH_EnemyCrab.h"
#include "AI/BH_CrabAIController.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "Components/BH_TelegraphComponent.h"
#include "AbilitySystemComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"

ABH_EnemyCrab::ABH_EnemyCrab()
{
	DisplayName = NSLOCTEXT("BlackwoodHollow", "CrabDefaultName", "Hollow Crab");

	Telegraph = CreateDefaultSubobject<UBH_TelegraphComponent>(TEXT("Telegraph"));

	// The Behavior Tree drives the crab: no base-class test behaviours, no refill after death (it ragdolls and despawns).
	bFaceTarget = false;
	bResetOnDeath = false;
	AIControllerClass = ABH_CrabAIController::StaticClass();

	// Face the controller's focus (the target) while scuttling sideways: the anim instance blends by MoveDirectionAngle.
	bUseControllerRotationYaw = false;
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->bUseControllerDesiredRotation = true;
		Movement->bOrientRotationToMovement = false;
		Movement->RotationRate = FRotator(0.f, 360.f, 0.f);
	}
}

void ABH_EnemyCrab::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABH_EnemyCrab, ActionPhase);
}

void ABH_EnemyCrab::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority() && AbilitySystemComponent)
	{
		ApplyInitialStats();

		PostureBrokenHandle = AbilitySystemComponent->RegisterGameplayTagEvent(TAG_State_Combat_PostureBroken, EGameplayTagEventType::NewOrRemoved)
			.AddUObject(this, &ABH_EnemyCrab::OnStatusTagChanged);
		StaggeredHandle = AbilitySystemComponent->RegisterGameplayTagEvent(TAG_State_Combat_Staggered, EGameplayTagEventType::NewOrRemoved)
			.AddUObject(this, &ABH_EnemyCrab::OnStatusTagChanged);
	}
}

void ABH_EnemyCrab::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AbilitySystemComponent)
	{
		if (PostureBrokenHandle.IsValid())
		{
			AbilitySystemComponent->RegisterGameplayTagEvent(TAG_State_Combat_PostureBroken, EGameplayTagEventType::NewOrRemoved).Remove(PostureBrokenHandle);
		}
		if (StaggeredHandle.IsValid())
		{
			AbilitySystemComponent->RegisterGameplayTagEvent(TAG_State_Combat_Staggered, EGameplayTagEventType::NewOrRemoved).Remove(StaggeredHandle);
		}
	}
	PostureBrokenHandle.Reset();
	StaggeredHandle.Reset();
	Super::EndPlay(EndPlayReason);
}

// ============================================================================
// Stats
// ============================================================================

void ABH_EnemyCrab::ApplyInitialStats_Implementation()
{
	if (!bApplyStatOverrides || !AbilitySystemComponent)
	{
		return;
	}

	// Max first, then current (the attribute set clamps current to max).
	AbilitySystemComponent->SetNumericAttributeBase(UAH_AttributeSet::GetMaxHealthAttribute(), InitialMaxHealth);
	AbilitySystemComponent->SetNumericAttributeBase(UAH_AttributeSet::GetHealthAttribute(), InitialMaxHealth);
	AbilitySystemComponent->SetNumericAttributeBase(UAH_AttributeSet::GetMaxPostureAttribute(), InitialMaxPosture);
	AbilitySystemComponent->SetNumericAttributeBase(UAH_AttributeSet::GetPostureAttribute(), InitialMaxPosture);
	if (InitialAttackPower >= 0.f)
	{
		AbilitySystemComponent->SetNumericAttributeBase(UAH_AttributeSet::GetAttackPowerAttribute(), InitialAttackPower);
	}
	if (InitialDefense >= 0.f)
	{
		AbilitySystemComponent->SetNumericAttributeBase(UAH_AttributeSet::GetDefenseAttribute(), InitialDefense);
	}
}

// ============================================================================
// Action phase
// ============================================================================

bool ABH_EnemyCrab::IsBlighted() const
{
	return AbilitySystemComponent && AbilitySystemComponent->HasMatchingGameplayTag(TAG_State_Status_Blighted);
}

void ABH_EnemyCrab::SetAbilityPhase(EBH_CrabActionPhase NewPhase)
{
	if (!HasAuthority())
	{
		return;
	}
	AbilityPhase = NewPhase;
	RecomputeActionPhase();
}

void ABH_EnemyCrab::OnStatusTagChanged(const FGameplayTag Tag, int32 NewCount)
{
	RecomputeActionPhase();
}

void ABH_EnemyCrab::RecomputeActionPhase()
{
	if (!HasAuthority())
	{
		return;
	}

	EBH_CrabActionPhase NewPhase = AbilityPhase;
	if (bDeadFlag)
	{
		NewPhase = EBH_CrabActionPhase::Dead;
	}
	else if (AbilitySystemComponent && AbilitySystemComponent->HasMatchingGameplayTag(TAG_State_Combat_PostureBroken))
	{
		NewPhase = EBH_CrabActionPhase::Stagger;
	}
	else if (AbilitySystemComponent && AbilitySystemComponent->HasMatchingGameplayTag(TAG_State_Combat_Staggered))
	{
		NewPhase = EBH_CrabActionPhase::HitReact;
	}

	if (NewPhase != ActionPhase)
	{
		ActionPhase = NewPhase;
		OnRep_ActionPhase(); // the server (and a listen-server host) does not get the RepNotify
		ForceNetUpdate();
	}
}

void ABH_EnemyCrab::OnRep_ActionPhase()
{
	if (ActionPhase == EBH_CrabActionPhase::Dead)
	{
		StartRagdoll();
	}
}

float ABH_EnemyCrab::GetSecondsSinceLastSidestep() const
{
	const UWorld* World = GetWorld();
	return World ? static_cast<float>(World->GetTimeSeconds() - LastSidestepTime) : 0.f;
}

void ABH_EnemyCrab::MarkSidestepUsed()
{
	if (const UWorld* World = GetWorld())
	{
		LastSidestepTime = World->GetTimeSeconds();
	}
}

// ============================================================================
// Death / ragdoll
// ============================================================================

void ABH_EnemyCrab::OnDeathNative(AActor* Killer)
{
	bDeadFlag = true;
	RecomputeActionPhase(); // -> Dead: replicates, and starts the ragdoll right here on the server

	// The server's CMC no longer drives the body; clients simulate their own ragdoll.
	SetReplicateMovement(false);
	StartRagdoll();

	DetachFromControllerPendingDestroy();
	SetLifeSpan(DespawnDelay);
}

void ABH_EnemyCrab::StartRagdoll()
{
	if (bRagdolling)
	{
		return;
	}
	bRagdolling = true;

	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
		Movement->SetComponentTickEnabled(false);
	}

	USkeletalMeshComponent* SkelMesh = GetMesh();
	if (SkelMesh)
	{
		if (SkelMesh->GetPhysicsAsset())
		{
			SkelMesh->SetCollisionProfileName(RagdollCollisionProfile);
			SkelMesh->SetAllBodiesSimulatePhysics(true);
			SkelMesh->SetSimulatePhysics(true);
			SkelMesh->WakeAllRigidBodies();
		}
		else
		{
			UE_LOG(LogBHCombat, Warning, TEXT("%s: no physics asset on the mesh, the crab cannot ragdoll (it just stops)."), *GetNameSafe(this));
		}
	}

	K2_OnRagdollStarted();
}
