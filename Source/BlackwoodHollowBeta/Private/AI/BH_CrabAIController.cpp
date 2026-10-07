// Blackwood Hollow - crab AI controller (implementation)

#include "AI/BH_CrabAIController.h"
#include "AI/BH_AttackTokenSubsystem.h"
#include "Characters/BH_EnemyCrab.h"
#include "Combat/BH_CombatIdentityComponent.h"
#include "Combat/BH_CombatTeam.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystem/BH_CombatFunctionLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BrainComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GenericTeamAgentInterface.h"
#include "GameFramework/Pawn.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"

ABH_CrabAIController::ABH_CrabAIController()
{
	PrimaryActorTick.bCanEverTick = false;
	SetGenericTeamId(BH_CombatTeam::ToGenericTeamId(EBH_CombatTeam::Enemies));
}

// ============================================================================
// Possession
// ============================================================================

void ABH_CrabAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	if (const UBH_CombatIdentityComponent* Identity = UBH_CombatIdentityComponent::Find(InPawn))
	{
		SetGenericTeamId(BH_CombatTeam::ToGenericTeamId(Identity->CombatTeam));
	}
	else if (const IGenericTeamAgentInterface* PawnAgent = Cast<IGenericTeamAgentInterface>(InPawn))
	{
		SetGenericTeamId(PawnAgent->GetGenericTeamId());
	}

	StartBrain();

	if (UWorld* World = GetWorld())
	{
		// Stagger the first scans so a pack does not scan on the same frame.
		const float Jitter = FMath::FRandRange(0.f, ScanInterval);
		World->GetTimerManager().SetTimer(ScanTimerHandle, this, &ABH_CrabAIController::ScanForTarget, ScanInterval, true, Jitter);
		World->GetTimerManager().SetTimer(BlackboardTimerHandle, this, &ABH_CrabAIController::UpdateBlackboard, BlackboardInterval, true, 0.f);
	}
}

void ABH_CrabAIController::OnUnPossess()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ScanTimerHandle);
		World->GetTimerManager().ClearTimer(BlackboardTimerHandle);
	}
	ReleaseAttackToken();
	SetCrabTarget(nullptr);
	if (UBrainComponent* Brain = GetBrainComponent())
	{
		Brain->StopLogic(TEXT("Unpossessed"));
	}
	Super::OnUnPossess();
}

void ABH_CrabAIController::StartBrain()
{
	UBehaviorTree* Tree = nullptr;
	if (const ABH_EnemyCrab* Crab = Cast<ABH_EnemyCrab>(GetPawn()))
	{
		Tree = Crab->CrabBehaviorTree;
	}
	if (!Tree)
	{
		Tree = DefaultBehaviorTree;
	}
	if (!Tree)
	{
		UE_LOG(LogBHCombat, Warning, TEXT("%s: no behavior tree (set CrabBehaviorTree on the crab or DefaultBehaviorTree on the controller)"), *GetNameSafe(GetPawn()));
		return;
	}
	RunBehaviorTree(Tree);
}

// ============================================================================
// Queries
// ============================================================================

UAbilitySystemComponent* ABH_CrabAIController::GetPawnASC() const
{
	return UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetPawn());
}

bool ABH_CrabAIController::IsTargetGuarding() const
{
	const UAbilitySystemComponent* ASC = WatchedTargetASC.Get();
	return ASC && (ASC->HasMatchingGameplayTag(TAG_State_Combat_Blocking) || ASC->HasMatchingGameplayTag(TAG_State_Combat_Parrying));
}

float ABH_CrabAIController::GetSecondsSinceTargetStartedCombo() const
{
	if (LastTargetAttackStartTime < 0.0)
	{
		return TNumericLimits<float>::Max();
	}
	const UWorld* World = GetWorld();
	return World ? static_cast<float>(World->GetTimeSeconds() - LastTargetAttackStartTime) : TNumericLimits<float>::Max();
}

float ABH_CrabAIController::GetDistanceToCrabTarget() const
{
	const APawn* Me = GetPawn();
	const AActor* T = CrabTarget.Get();
	return (Me && T) ? FVector::Dist2D(Me->GetActorLocation(), T->GetActorLocation()) : TNumericLimits<float>::Max();
}

// ============================================================================
// Attack token
// ============================================================================

bool ABH_CrabAIController::HasAttackToken() const
{
	if (!bUseAttackTokens || !UBH_AttackTokenSubsystem::bEnableAttackTokens)
	{
		return true;
	}
	const AActor* T = CrabTarget.Get();
	const UBH_AttackTokenSubsystem* Tokens = T ? UBH_AttackTokenSubsystem::Get(this) : nullptr;
	return Tokens && Tokens->HasToken(T, this);
}

bool ABH_CrabAIController::RequestAttackToken()
{
	if (!bUseAttackTokens || !UBH_AttackTokenSubsystem::bEnableAttackTokens)
	{
		return true;
	}
	AActor* T = CrabTarget.Get();
	UBH_AttackTokenSubsystem* Tokens = UBH_AttackTokenSubsystem::Get(this);
	if (!T || !Tokens)
	{
		return false; // no target: nothing to attack
	}
	bHoldingToken = Tokens->RequestToken(T, this);
	return bHoldingToken;
}

void ABH_CrabAIController::ReleaseAttackToken()
{
	AActor* T = CrabTarget.Get();
	if (UBH_AttackTokenSubsystem* Tokens = T ? UBH_AttackTokenSubsystem::Get(this) : nullptr)
	{
		Tokens->ReleaseToken(T, this);
	}
	bHoldingToken = false;
}

// ============================================================================
// Targeting
// ============================================================================

bool ABH_CrabAIController::IsValidHostile(const APawn* Candidate, float MaxRange) const
{
	const APawn* Me = GetPawn();
	if (!Me || !IsValid(Candidate) || Candidate == Me)
	{
		return false;
	}
	const UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(const_cast<APawn*>(Candidate));
	if (!ASC || ASC->HasMatchingGameplayTag(TAG_State_Combat_Dead))
	{
		return false;
	}
	if (ASC->HasAttributeSetForAttribute(UAH_AttributeSet::GetHealthAttribute())
		&& ASC->GetNumericAttribute(UAH_AttributeSet::GetHealthAttribute()) <= 0.f)
	{
		return false;
	}
	if (UBH_CombatFunctionLibrary::AreCombatAllies(Me, Candidate))
	{
		return false;
	}
	return FVector::Dist(Me->GetActorLocation(), Candidate->GetActorLocation()) <= MaxRange;
}

void ABH_CrabAIController::ScanForTarget()
{
	const APawn* Me = GetPawn();
	UWorld* World = GetWorld();
	if (!Me || !World)
	{
		return;
	}

	// Drop a target that died or wandered off.
	AActor* Current = CrabTarget.Get();
	if (Current && !IsValidHostile(Cast<APawn>(Current), LoseTargetRange))
	{
		SetCrabTarget(nullptr);
		Current = nullptr;
	}

	const APawn* Best = nullptr;
	float BestDist = TNumericLimits<float>::Max();
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		const APawn* Candidate = *It;
		if (!IsValidHostile(Candidate, AggroRange))
		{
			continue;
		}
		const float Dist = FVector::Dist(Me->GetActorLocation(), Candidate->GetActorLocation());
		if (Dist >= BestDist)
		{
			continue;
		}

		FCollisionQueryParams Params(SCENE_QUERY_STAT(BH_CrabAcquireLOS), false);
		Params.AddIgnoredActor(Me);
		Params.AddIgnoredActor(Candidate);
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, Me->GetActorLocation(), Candidate->GetActorLocation(), ECC_Visibility, Params))
		{
			continue;
		}
		Best = Candidate;
		BestDist = Dist;
	}

	if (Best && Best != Current)
	{
		// Only switch to a clearly nearer hostile (or when there is no current target).
		if (!Current || BestDist + 150.f < GetDistanceToCrabTarget())
		{
			SetCrabTarget(const_cast<APawn*>(Best));
		}
	}
}

void ABH_CrabAIController::SetCrabTarget(AActor* NewTarget)
{
	if (CrabTarget.Get() == NewTarget)
	{
		return;
	}

	// A token is per target: give the old target's back before switching.
	ReleaseAttackToken();

	if (UAbilitySystemComponent* OldASC = WatchedTargetASC.Get())
	{
		OldASC->RegisterGameplayTagEvent(TAG_State_Combat_Attacking, EGameplayTagEventType::NewOrRemoved).Remove(TargetAttackingHandle);
	}
	TargetAttackingHandle.Reset();
	WatchedTargetASC = nullptr;
	LastTargetAttackStartTime = -1.0;

	CrabTarget = NewTarget;

	if (NewTarget)
	{
		if (UAbilitySystemComponent* NewASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(NewTarget))
		{
			TargetAttackingHandle = NewASC->RegisterGameplayTagEvent(TAG_State_Combat_Attacking, EGameplayTagEventType::NewOrRemoved)
				.AddUObject(this, &ABH_CrabAIController::OnTargetAttackingChanged);
			WatchedTargetASC = NewASC;
		}
		SetFocus(NewTarget);
	}
	else
	{
		ClearFocus(EAIFocusPriority::Gameplay);
	}

	if (UBH_CombatIdentityComponent* Identity = UBH_CombatIdentityComponent::Find(GetPawn()))
	{
		Identity->SetAggroTarget(NewTarget);
	}

	UpdateBlackboard();
}

void ABH_CrabAIController::OnTargetAttackingChanged(const FGameplayTag Tag, int32 NewCount)
{
	if (NewCount > 0)
	{
		if (const UWorld* World = GetWorld())
		{
			LastTargetAttackStartTime = World->GetTimeSeconds();
		}
		UpdateBlackboard();
	}
}

// ============================================================================
// Blackboard
// ============================================================================

void ABH_CrabAIController::UpdateBlackboard()
{
	UBlackboardComponent* BB = GetBlackboardComponent();
	if (!BB)
	{
		return;
	}

	AActor* T = CrabTarget.Get();
	if (T)
	{
		BB->SetValueAsObject(BH_CrabBB::TargetActor, T);
		BB->SetValueAsVector(BH_CrabBB::TargetLocation, T->GetActorLocation());
	}
	else
	{
		BB->ClearValue(BH_CrabBB::TargetActor);
		BB->ClearValue(BH_CrabBB::TargetLocation);
	}
	BB->SetValueAsFloat(BH_CrabBB::DistanceToTarget, GetDistanceToCrabTarget());
	BB->SetValueAsBool(BH_CrabBB::bTargetGuarding, T && IsTargetGuarding());
	BB->SetValueAsBool(BH_CrabBB::bTargetStartedCombo, T && GetSecondsSinceTargetStartedCombo() <= TargetComboWindow);
	BB->SetValueAsBool(BH_CrabBB::bHasToken, T && HasAttackToken());
}
