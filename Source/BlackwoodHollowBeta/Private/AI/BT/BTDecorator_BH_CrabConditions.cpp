// Blackwood Hollow - crab Behavior Tree decorators (implementation)

#include "AI/BT/BTDecorator_BH_CrabConditions.h"
#include "AI/BH_CrabAIController.h"
#include "Characters/BH_EnemyCrab.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "Engine/World.h"

// ============================================================================
// Base
// ============================================================================

UBTDecorator_BH_CrabConditionBase::UBTDecorator_BH_CrabConditionBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NodeName = TEXT("BH Crab Condition");
	INIT_DECORATOR_NODE_NOTIFY_FLAGS();
	bAllowAbortNone = true;
	bAllowAbortLowerPri = true;
	bAllowAbortChildNodes = true;
	FlowAbortMode = EBTFlowAbortMode::None;
}

void UBTDecorator_BH_CrabConditionBase::InitializeMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryInit::Type InitType) const
{
	FBTBHCrabConditionMemory* Memory = CastInstanceNodeMemory<FBTBHCrabConditionMemory>(NodeMemory);
	Memory->bLastResult = false;
}

bool UBTDecorator_BH_CrabConditionBase::CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
	const ABH_CrabAIController* Controller = Cast<ABH_CrabAIController>(OwnerComp.GetAIOwner());
	return Controller && EvaluateCrabCondition(OwnerComp, *Controller);
}

void UBTDecorator_BH_CrabConditionBase::OnBecomeRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FBTBHCrabConditionMemory* Memory = CastInstanceNodeMemory<FBTBHCrabConditionMemory>(NodeMemory);
	Memory->bLastResult = CalculateRawConditionValue(OwnerComp, NodeMemory);
}

void UBTDecorator_BH_CrabConditionBase::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FBTBHCrabConditionMemory* Memory = CastInstanceNodeMemory<FBTBHCrabConditionMemory>(NodeMemory);
	const bool bNow = CalculateRawConditionValue(OwnerComp, NodeMemory);
	if (bNow != Memory->bLastResult)
	{
		Memory->bLastResult = bNow;
		if (FlowAbortMode != EBTFlowAbortMode::None)
		{
			ConditionalFlowAbort(OwnerComp, EBTDecoratorAbortRequest::ConditionResultChanged);
		}
	}
}

// ============================================================================
// Target Is Guarding
// ============================================================================

UBTDecorator_BH_TargetIsGuarding::UBTDecorator_BH_TargetIsGuarding(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NodeName = TEXT("BH Target Is Guarding");
}

FString UBTDecorator_BH_TargetIsGuarding::GetStaticDescription() const
{
	return TEXT("Target is Blocking or Parrying");
}

bool UBTDecorator_BH_TargetIsGuarding::EvaluateCrabCondition(UBehaviorTreeComponent& OwnerComp, const ABH_CrabAIController& Controller) const
{
	return Controller.IsTargetGuarding();
}

// ============================================================================
// Target Started Combo
// ============================================================================

UBTDecorator_BH_TargetStartedCombo::UBTDecorator_BH_TargetStartedCombo(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NodeName = TEXT("BH Target Started Combo");
}

FString UBTDecorator_BH_TargetStartedCombo::GetStaticDescription() const
{
	return FString::Printf(TEXT("Target started an attack within the last %.2f s"), Window);
}

bool UBTDecorator_BH_TargetStartedCombo::EvaluateCrabCondition(UBehaviorTreeComponent& OwnerComp, const ABH_CrabAIController& Controller) const
{
	return Controller.GetCrabTarget() && Controller.GetSecondsSinceTargetStartedCombo() <= Window;
}

// ============================================================================
// Sidestep Gate
// ============================================================================

UBTDecorator_BH_SidestepGate::UBTDecorator_BH_SidestepGate(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NodeName = TEXT("BH Sidestep Gate");
	FlowAbortMode = EBTFlowAbortMode::None;
}

void UBTDecorator_BH_SidestepGate::InitializeMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryInit::Type InitType) const
{
	FBTBHSidestepGateMemory* Memory = CastInstanceNodeMemory<FBTBHSidestepGateMemory>(NodeMemory);
	Memory->RollTime = -1.0;
	Memory->bHasRoll = false;
	Memory->bRollResult = false;
}

FString UBTDecorator_BH_SidestepGate::GetStaticDescription() const
{
	return FString::Printf(TEXT("%.0f%% chance, %.1f s cooldown"), Chance * 100.f, Cooldown);
}

bool UBTDecorator_BH_SidestepGate::CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
	const ABH_CrabAIController* Controller = Cast<ABH_CrabAIController>(OwnerComp.GetAIOwner());
	const ABH_EnemyCrab* Crab = Controller ? Cast<ABH_EnemyCrab>(Controller->GetPawn()) : nullptr;
	if (!Crab || !Controller->GetCrabTarget())
	{
		return false;
	}
	if (Crab->GetSecondsSinceLastSidestep() < Cooldown)
	{
		return false;
	}
	if (const UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(const_cast<ABH_EnemyCrab*>(Crab)))
	{
		if (ASC->HasMatchingGameplayTag(TAG_State_Combat_Attacking) || ASC->HasMatchingGameplayTag(TAG_State_Combat_Staggered)
			|| ASC->HasMatchingGameplayTag(TAG_State_Combat_PostureBroken) || ASC->HasMatchingGameplayTag(TAG_State_Combat_Dead)
			|| ASC->HasMatchingGameplayTag(TAG_State_Combat_Dodging))
		{
			return false;
		}
	}

	// Roll once per combo window: while the target's current combo is still inside the window and the last roll was made
	// during that same combo, reuse its result instead of re-rolling every evaluation.
	FBTBHSidestepGateMemory* Memory = CastInstanceNodeMemory<FBTBHSidestepGateMemory>(NodeMemory);
	const double Now = Crab->GetWorld() ? Crab->GetWorld()->GetTimeSeconds() : 0.0;
	const float SinceCombo = Controller->GetSecondsSinceTargetStartedCombo();
	const bool bInWindow = SinceCombo < Controller->TargetComboWindow;
	if (bInWindow && Memory->bHasRoll && (Now - Memory->RollTime) <= static_cast<double>(SinceCombo) + KINDA_SMALL_NUMBER)
	{
		return Memory->bRollResult;
	}

	const bool bRoll = FMath::FRand() < Chance;
	Memory->RollTime = Now;
	Memory->bHasRoll = true;
	Memory->bRollResult = bRoll;
	return bRoll;
}

// ============================================================================
// Target Within
// ============================================================================

UBTDecorator_BH_TargetWithin::UBTDecorator_BH_TargetWithin(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NodeName = TEXT("BH Target Within");
	FlowAbortMode = EBTFlowAbortMode::None;
}

FString UBTDecorator_BH_TargetWithin::GetStaticDescription() const
{
	return FString::Printf(TEXT("Target within %.0f cm"), MaxDistance);
}

bool UBTDecorator_BH_TargetWithin::CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
	const ABH_CrabAIController* Controller = Cast<ABH_CrabAIController>(OwnerComp.GetAIOwner());
	return Controller && Controller->GetCrabTarget() && Controller->GetDistanceToCrabTarget() <= MaxDistance;
}

// ============================================================================
// Has Target
// ============================================================================

UBTDecorator_BH_HasTarget::UBTDecorator_BH_HasTarget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NodeName = TEXT("BH Has Target");
	FlowAbortMode = EBTFlowAbortMode::None;
}

FString UBTDecorator_BH_HasTarget::GetStaticDescription() const
{
	return TEXT("Controller has a target");
}

bool UBTDecorator_BH_HasTarget::CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
	const ABH_CrabAIController* Controller = Cast<ABH_CrabAIController>(OwnerComp.GetAIOwner());
	return Controller && Controller->GetCrabTarget();
}
