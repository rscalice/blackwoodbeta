// Blackwood Hollow - crab AI controller (implementation)

#include "AI/BH_CrabAIController.h"
#include "AI/BH_AttackTokenSubsystem.h"
#include "Characters/BH_EnemyCrab.h"
#include "Characters/BH_EnemyBase.h"
#include "Combat/BH_CombatIdentityComponent.h"
#include "Combat/BH_CombatTeam.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystem/BH_CombatFunctionLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AI/BT/BTDecorator_BH_CrabConditions.h"
#include "AI/BT/BTTask_BH_CrabTasks.h"
#include "AbilitySystem/Abilities/AH_GA_CrabAbilities.h"
#include "Abilities/GameplayAbility.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/Composites/BTComposite_Selector.h"
#include "BehaviorTree/Composites/BTComposite_Sequence.h"
#include "BrainComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "HAL/IConsoleManager.h"
#include "GenericTeamAgentInterface.h"
#include "GameFramework/Pawn.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"

ABH_CrabAIController::ABH_CrabAIController()
{
	PrimaryActorTick.bCanEverTick = false;
	SetGenericTeamId(BH_CombatTeam::ToGenericTeamId(EBH_CombatTeam::Enemies));
	CodeTreeBlackboard = TSoftObjectPtr<UBlackboardData>(FSoftObjectPath(TEXT("/Game/BlackwoodHollow/Characters/Enemies/Crab/AI/BB_BH_Crab.BB_BH_Crab")));
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
	if (bUseCodeBuiltTree)
	{
		// The BT_BH_Crab asset is corrupted and cannot be authored in this build: run the tree assembled in C++ (no asset validation needed).
		UBehaviorTree* CodeTree = BuildCrabTree();
		if (!CodeTree)
		{
			UE_LOG(LogBHCombat, Error, TEXT("%s: could not build the code-built behavior tree, the crab has no brain"), *GetNameSafe(GetPawn()));
			return;
		}
		if (!RunBehaviorTree(CodeTree))
		{
			UE_LOG(LogBHCombat, Warning, TEXT("%s: RunBehaviorTree failed for the code-built tree"), *GetNameSafe(GetPawn()));
		}
		return;
	}

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
	ValidateBehaviorTree(Tree);
}

namespace
{
	/** Assembles the crab tree's node objects. Every node is outered to the tree (BehaviorTreeManager::LoadTree duplicates them into its template). */
	struct FCrabTreeBuilder
	{
		explicit FCrabTreeBuilder(UBehaviorTree& InTree) : Tree(InTree) {}

		UBehaviorTree& Tree;
		int32 NodeCount = 0;

		template <class TNode>
		TNode* Make(const FString& Name)
		{
			TNode* Node = NewObject<TNode>(&Tree);
			Node->NodeName = Name;
			++NodeCount;
			return Node;
		}

		/** Decorators of one child are ANDed: the editor's compiled form is [Test(0)] for one decorator, [And(N), Test(0) ... Test(N-1)] for N. */
		static void SetDecorators(FBTCompositeChild& Child, const TArray<UBTDecorator*>& Decorators)
		{
			for (UBTDecorator* Decorator : Decorators)
			{
				Child.Decorators.Add(Decorator);
			}
			if (Decorators.Num() > 1)
			{
				Child.DecoratorOps.Add(FBTDecoratorLogic(static_cast<uint8>(EBTDecoratorLogic::And), static_cast<uint16>(Decorators.Num())));
			}
			for (int32 Index = 0; Index < Decorators.Num(); ++Index)
			{
				Child.DecoratorOps.Add(FBTDecoratorLogic(static_cast<uint8>(EBTDecoratorLogic::Test), static_cast<uint16>(Index)));
			}
		}

		static void AddTask(UBTCompositeNode* Parent, UBTTaskNode* Task, const TArray<UBTDecorator*>& Decorators = TArray<UBTDecorator*>())
		{
			FBTCompositeChild& Child = Parent->Children.AddDefaulted_GetRef();
			Child.ChildTask = Task;
			SetDecorators(Child, Decorators);
		}

		static void AddComposite(UBTCompositeNode* Parent, UBTCompositeNode* Composite, const TArray<UBTDecorator*>& Decorators = TArray<UBTDecorator*>())
		{
			FBTCompositeChild& Child = Parent->Children.AddDefaulted_GetRef();
			Child.ChildComposite = Composite;
			SetDecorators(Child, Decorators);
		}
	};
}

UBehaviorTree* ABH_CrabAIController::BuildCrabTree()
{
	if (CodeBuiltTree)
	{
		return CodeBuiltTree;
	}

	UBlackboardData* CrabBB = CodeTreeBlackboard.LoadSynchronous();
	if (!CrabBB)
	{
		UE_LOG(LogBHCombat, Error, TEXT("%s: cannot load the crab blackboard %s"), *GetNameSafe(GetPawn()), *CodeTreeBlackboard.ToString());
		return nullptr;
	}

	// The tree reads the controller directly, but the controller still mirrors these keys: warn about any the asset lacks.
	const FName RequiredKeys[] = { FName(TEXT("SelfActor")), BH_CrabBB::TargetActor, BH_CrabBB::TargetLocation, BH_CrabBB::DistanceToTarget,
		BH_CrabBB::bTargetGuarding, BH_CrabBB::bTargetStartedCombo, BH_CrabBB::FlankLocation, BH_CrabBB::bHasToken };
	for (const FName& KeyName : RequiredKeys)
	{
		if (CrabBB->GetKeyID(KeyName) == FBlackboard::InvalidKey)
		{
			UE_LOG(LogBHCombat, Warning, TEXT("%s: blackboard %s has no key '%s'"), *GetNameSafe(GetPawn()), *GetNameSafe(CrabBB), *KeyName.ToString());
		}
	}

	UBehaviorTree* Tree = NewObject<UBehaviorTree>(this, TEXT("BT_BH_Crab_Code"));
	Tree->BlackboardAsset = CrabBB;
	FCrabTreeBuilder B(*Tree);

	// Attack step shared by Jab and Pinch: Selector( Sequence(Activate ability, Release token), Release token ), so the token goes back
	// whether or not the ability activated.
	auto AddAttack = [&B](UBTCompositeNode* Parent, const FString& AttackName, TSubclassOf<UGameplayAbility> AbilityClass, float AbilityTimeout)
	{
		UBTComposite_Selector* Attack = B.Make<UBTComposite_Selector>(AttackName + TEXT(" (or release)"));
		FCrabTreeBuilder::AddComposite(Parent, Attack);

		UBTComposite_Sequence* Strike = B.Make<UBTComposite_Sequence>(AttackName);
		FCrabTreeBuilder::AddComposite(Attack, Strike);

		UBTTask_BH_ActivateAbilityByClass* Activate = B.Make<UBTTask_BH_ActivateAbilityByClass>(AttackName);
		Activate->AbilityClass = AbilityClass;
		Activate->bWaitForCompletion = true;
		Activate->Timeout = AbilityTimeout;
		FCrabTreeBuilder::AddTask(Strike, Activate);
		FCrabTreeBuilder::AddTask(Strike, B.Make<UBTTask_BH_ReleaseAttackToken>(TEXT("Release token")));

		FCrabTreeBuilder::AddTask(Attack, B.Make<UBTTask_BH_ReleaseAttackToken>(TEXT("Release (activation failed)")));
	};

	UBTComposite_Selector* Root = B.Make<UBTComposite_Selector>(TEXT("Crab Root"));
	Tree->RootNode = Root;

	// [0] Reaction: Sidestep (the target just started an attack; chance + cooldown gated)
	{
		UBTDecorator_BH_TargetStartedCombo* Started = B.Make<UBTDecorator_BH_TargetStartedCombo>(TEXT("Target started combo"));
		Started->Window = TargetComboWindow;
		UBTDecorator_BH_SidestepGate* Gate = B.Make<UBTDecorator_BH_SidestepGate>(TEXT("Sidestep gate"));
		Gate->Chance = 0.35f;
		Gate->Cooldown = 4.f;

		UBTComposite_Sequence* Reaction = B.Make<UBTComposite_Sequence>(TEXT("Reaction: Sidestep"));
		FCrabTreeBuilder::AddComposite(Root, Reaction, { Started, Gate });

		UBTTask_BH_ActivateAbilityByClass* Sidestep = B.Make<UBTTask_BH_ActivateAbilityByClass>(TEXT("Sidestep"));
		Sidestep->AbilityClass = UAH_GA_CrabSidestep::StaticClass();
		Sidestep->bWaitForCompletion = true;
		Sidestep->Timeout = 2.f;
		FCrabTreeBuilder::AddTask(Reaction, Sidestep);
	}

	// [1] Guarding: Flank then Jab (the target blocks / parries and is close)
	{
		UBTDecorator_BH_TargetIsGuarding* Guarding = B.Make<UBTDecorator_BH_TargetIsGuarding>(TEXT("Target is guarding"));
		UBTDecorator_BH_TargetWithin* Within = B.Make<UBTDecorator_BH_TargetWithin>(TEXT("Target within 600"));
		Within->MaxDistance = 600.f;

		UBTComposite_Sequence* Flank = B.Make<UBTComposite_Sequence>(TEXT("Guarding: Flank then Jab"));
		FCrabTreeBuilder::AddComposite(Root, Flank, { Guarding, Within });

		UBTTask_BH_FlankMove* FlankMove = B.Make<UBTTask_BH_FlankMove>(TEXT("Flank"));
		FlankMove->bOrbit = false;
		FCrabTreeBuilder::AddTask(Flank, FlankMove);

		UBTTask_BH_RequestAttackToken* Request = B.Make<UBTTask_BH_RequestAttackToken>(TEXT("Request token"));
		Request->MaxWaitTime = 0.75f;
		FCrabTreeBuilder::AddTask(Flank, Request);

		AddAttack(Flank, TEXT("Jab"), UAH_GA_CrabJab::StaticClass(), 2.f);
	}

	// [2] Open: Pinch (target in pinch range)
	{
		UBTDecorator_BH_TargetWithin* Within = B.Make<UBTDecorator_BH_TargetWithin>(TEXT("Target within 450"));
		Within->MaxDistance = 450.f;

		UBTComposite_Sequence* Open = B.Make<UBTComposite_Sequence>(TEXT("Open: Pinch"));
		FCrabTreeBuilder::AddComposite(Root, Open, { Within });

		UBTTask_BH_RequestAttackToken* Request = B.Make<UBTTask_BH_RequestAttackToken>(TEXT("Request token"));
		Request->MaxWaitTime = 0.75f;
		FCrabTreeBuilder::AddTask(Open, Request);

		AddAttack(Open, TEXT("Pinch"), UAH_GA_CrabPinch::StaticClass(), 3.f);
	}

	// [3] Pressure: Orbit (no token: circle the target, then pause)
	{
		UBTDecorator_BH_TargetWithin* Within = B.Make<UBTDecorator_BH_TargetWithin>(TEXT("Target within 650"));
		Within->MaxDistance = 650.f;

		UBTComposite_Sequence* Pressure = B.Make<UBTComposite_Sequence>(TEXT("Pressure: Orbit"));
		FCrabTreeBuilder::AddComposite(Root, Pressure, { Within });

		UBTTask_BH_FlankMove* Orbit = B.Make<UBTTask_BH_FlankMove>(TEXT("Orbit"));
		Orbit->bOrbit = true;
		Orbit->OrbitMinRadius = 250.f;
		Orbit->OrbitMaxRadius = 400.f;
		Orbit->OrbitAngleMin = 30.f;
		Orbit->OrbitAngleMax = 75.f;
		FCrabTreeBuilder::AddTask(Pressure, Orbit);

		UBTTask_BH_Wait* Pause = B.Make<UBTTask_BH_Wait>(TEXT("Pause"));
		Pause->WaitTime = 0.4f;
		Pause->RandomDeviation = 0.2f;
		FCrabTreeBuilder::AddTask(Pressure, Pause);
	}

	// [4] Chase (any target farther away)
	{
		UBTDecorator_BH_HasTarget* HasTarget = B.Make<UBTDecorator_BH_HasTarget>(TEXT("Has target"));

		UBTComposite_Sequence* Chase = B.Make<UBTComposite_Sequence>(TEXT("Chase"));
		FCrabTreeBuilder::AddComposite(Root, Chase, { HasTarget });

		UBTTask_BH_ChaseTarget* ChaseMove = B.Make<UBTTask_BH_ChaseTarget>(TEXT("Chase target"));
		ChaseMove->AcceptanceRadius = 120.f;
		FCrabTreeBuilder::AddTask(Chase, ChaseMove);

		UBTTask_BH_Wait* Pause = B.Make<UBTTask_BH_Wait>(TEXT("Pause"));
		Pause->WaitTime = 0.3f;
		Pause->RandomDeviation = 0.f;
		FCrabTreeBuilder::AddTask(Chase, Pause);
	}

	// [5] Idle: nothing applies (no target, or a chase just failed). Without it the looped root would fail and re-search every frame.
	{
		UBTTask_BH_Wait* Idle = B.Make<UBTTask_BH_Wait>(TEXT("Idle"));
		Idle->WaitTime = 0.25f;
		Idle->RandomDeviation = 0.f;
		FCrabTreeBuilder::AddTask(Root, Idle);
	}

	CodeBuiltTree = Tree;
	UE_LOG(LogBHCombat, Log, TEXT("Crab %s running code-built BT (%d nodes)"), *GetNameSafe(GetPawn()), B.NodeCount);
	return CodeBuiltTree;
}

void ABH_CrabAIController::ValidateBehaviorTree(const UBehaviorTree* Tree)
{
	if (bLoggedCorruptTree || !Tree)
	{
		return;
	}

	FString Problem;
	if (!Tree->RootNode)
	{
		Problem = TEXT("no root node");
	}
	else
	{
		for (int32 ChildIndex = 0; ChildIndex < Tree->RootNode->Children.Num() && Problem.IsEmpty(); ++ChildIndex)
		{
			const FBTCompositeChild& Child = Tree->RootNode->Children[ChildIndex];
			if (!Child.ChildComposite && !Child.ChildTask)
			{
				Problem = FString::Printf(TEXT("root child %d has neither a ChildComposite nor a ChildTask"), ChildIndex);
				break;
			}
			for (int32 DecoratorIndex = 0; DecoratorIndex < Child.Decorators.Num(); ++DecoratorIndex)
			{
				if (!Child.Decorators[DecoratorIndex])
				{
					Problem = FString::Printf(TEXT("root child %d has a null decorator (index %d)"), ChildIndex, DecoratorIndex);
					break;
				}
			}
		}
	}

	if (!Problem.IsEmpty())
	{
		bLoggedCorruptTree = true;
		UE_LOG(LogBHCombat, Error, TEXT("BT %s is corrupted: %s (crab %s). Rebuild the Behavior Tree asset."), *GetNameSafe(Tree), *Problem, *GetNameSafe(GetPawn()));
	}
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
	if (!Me || !World || bResetHold)
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
	else if (ABH_EnemyBase* EnemyBase = Cast<ABH_EnemyBase>(GetPawn()))
	{
		// Phase 11F (#21): the crab has no identity component; ABH_EnemyBase carries its aggro for the party-combat state.
		EnemyBase->SetAggroTarget(NewTarget);
	}

	UpdateBlackboard();
}

void ABH_CrabAIController::ResetAI()
{
	ReleaseAttackToken();
	SetCrabTarget(nullptr);
	StopMovement();
	if (UBrainComponent* Brain = GetBrainComponent())
	{
		Brain->RestartLogic();
	}
}

void ABH_CrabAIController::SetResetHold(bool bHold)
{
	if (bHold == bResetHold)
	{
		return;
	}
	bResetHold = bHold;
	if (bHold)
	{
		ResetAI();
	}
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

// ============================================================================
// Debug
// ============================================================================

#if !UE_BUILD_SHIPPING
namespace
{
	void DumpCrabBehaviorTrees(UWorld* World)
	{
		if (!World)
		{
			return;
		}
		int32 Count = 0;
		for (TActorIterator<ABH_CrabAIController> It(World); It; ++It)
		{
			const ABH_CrabAIController* Controller = *It;
			const UBehaviorTreeComponent* BTComp = Cast<UBehaviorTreeComponent>(Controller->GetBrainComponent());
			const UBlackboardComponent* BB = Controller->GetBlackboardComponent();
			const AActor* Target = Controller->GetCrabTarget();

			FString Active = BTComp ? BTComp->DescribeActiveTasks() : FString(TEXT("<no behavior tree component>"));
			FString Info = BTComp ? BTComp->GetDebugInfoString() : FString();
			Info.ReplaceInline(TEXT("\r"), TEXT(""));
			Info.ReplaceInline(TEXT("\n"), TEXT(" | "));

			UE_LOG(LogBHCombat, Log, TEXT("bh.Crab.DumpBT: %s (pawn %s) codeTree=%d running=%d active=[%s] target=%s dist=%.0f bHasToken=%d bbHasToken=%d | %s"),
				*GetNameSafe(Controller), *GetNameSafe(Controller->GetPawn()), Controller->bUseCodeBuiltTree ? 1 : 0, (BTComp && BTComp->IsRunning()) ? 1 : 0,
				*Active, *GetNameSafe(Target), Target ? Controller->GetDistanceToCrabTarget() : -1.f, Controller->HasAttackToken() ? 1 : 0,
				(BB && BB->GetValueAsBool(BH_CrabBB::bHasToken)) ? 1 : 0, *Info);
			++Count;
		}
		UE_LOG(LogBHCombat, Log, TEXT("bh.Crab.DumpBT: %d crab controller(s)."), Count);
	}

	static FAutoConsoleCommandWithWorld CmdDumpCrabBT(
		TEXT("bh.Crab.DumpBT"),
		TEXT("Logs every crab AI controller's active behavior tree node, target, distance and attack-token state."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&DumpCrabBehaviorTrees));
}
#endif
