// Blackwood Hollow - crab Behavior Tree tasks (implementation)

#include "AI/BT/BTTask_BH_CrabTasks.h"
#include "AI/BH_CrabAIController.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Abilities/GameplayAbility.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"

// ============================================================================
// Request Attack Token
// ============================================================================

UBTTask_BH_RequestAttackToken::UBTTask_BH_RequestAttackToken(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NodeName = TEXT("BH Request Attack Token");
	INIT_TASK_NODE_NOTIFY_FLAGS();
}

FString UBTTask_BH_RequestAttackToken::GetStaticDescription() const
{
	return FString::Printf(TEXT("Wait up to %.1f s for the target's attack token"), MaxWaitTime);
}

EBTNodeResult::Type UBTTask_BH_RequestAttackToken::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	ABH_CrabAIController* Controller = Cast<ABH_CrabAIController>(OwnerComp.GetAIOwner());
	if (!Controller)
	{
		return EBTNodeResult::Failed;
	}
	FBTBHCrabTaskMemory* Memory = CastInstanceNodeMemory<FBTBHCrabTaskMemory>(NodeMemory);
	Memory->Elapsed = 0.f;

	if (Controller->RequestAttackToken())
	{
		return EBTNodeResult::Succeeded;
	}
	if (!Controller->GetCrabTarget() || MaxWaitTime <= 0.f)
	{
		return EBTNodeResult::Failed;
	}
	return EBTNodeResult::InProgress;
}

void UBTTask_BH_RequestAttackToken::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	ABH_CrabAIController* Controller = Cast<ABH_CrabAIController>(OwnerComp.GetAIOwner());
	if (!Controller || !Controller->GetCrabTarget())
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	FBTBHCrabTaskMemory* Memory = CastInstanceNodeMemory<FBTBHCrabTaskMemory>(NodeMemory);
	Memory->Elapsed += DeltaSeconds;

	if (Controller->RequestAttackToken())
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
	}
	else if (Memory->Elapsed >= MaxWaitTime)
	{
		Controller->ReleaseAttackToken(); // drop the waiter entry so a quitter never blocks the queue
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}
}

EBTNodeResult::Type UBTTask_BH_RequestAttackToken::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	if (ABH_CrabAIController* Controller = Cast<ABH_CrabAIController>(OwnerComp.GetAIOwner()))
	{
		Controller->ReleaseAttackToken();
	}
	return EBTNodeResult::Aborted;
}

// ============================================================================
// Release Attack Token
// ============================================================================

UBTTask_BH_ReleaseAttackToken::UBTTask_BH_ReleaseAttackToken(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NodeName = TEXT("BH Release Attack Token");
	INIT_TASK_NODE_NOTIFY_FLAGS();
}

FString UBTTask_BH_ReleaseAttackToken::GetStaticDescription() const
{
	return TEXT("Give the attack token back");
}

EBTNodeResult::Type UBTTask_BH_ReleaseAttackToken::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	if (ABH_CrabAIController* Controller = Cast<ABH_CrabAIController>(OwnerComp.GetAIOwner()))
	{
		Controller->ReleaseAttackToken();
		return EBTNodeResult::Succeeded;
	}
	return EBTNodeResult::Failed;
}

// ============================================================================
// Activate Ability By Class
// ============================================================================

UBTTask_BH_ActivateAbilityByClass::UBTTask_BH_ActivateAbilityByClass(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NodeName = TEXT("BH Activate Ability By Class");
	INIT_TASK_NODE_NOTIFY_FLAGS();
}

FString UBTTask_BH_ActivateAbilityByClass::GetStaticDescription() const
{
	return FString::Printf(TEXT("%s%s"), *GetNameSafe(AbilityClass.Get()), bWaitForCompletion ? TEXT(" (wait for end)") : TEXT(""));
}

EBTNodeResult::Type UBTTask_BH_ActivateAbilityByClass::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	const AAIController* AIOwner = OwnerComp.GetAIOwner();
	UAbilitySystemComponent* ASC = AIOwner ? UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(AIOwner->GetPawn()) : nullptr;
	if (!ASC || !AbilityClass)
	{
		return EBTNodeResult::Failed;
	}

	FBTBHCrabTaskMemory* Memory = CastInstanceNodeMemory<FBTBHCrabTaskMemory>(NodeMemory);
	Memory->Elapsed = 0.f;

	if (!ASC->TryActivateAbilityByClass(AbilityClass))
	{
		return EBTNodeResult::Failed;
	}
	if (!bWaitForCompletion)
	{
		return EBTNodeResult::Succeeded;
	}

	const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromClass(AbilityClass);
	return (Spec && Spec->IsActive()) ? EBTNodeResult::InProgress : EBTNodeResult::Succeeded;
}

void UBTTask_BH_ActivateAbilityByClass::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	const AAIController* AIOwner = OwnerComp.GetAIOwner();
	UAbilitySystemComponent* ASC = AIOwner ? UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(AIOwner->GetPawn()) : nullptr;
	if (!ASC)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromClass(AbilityClass);
	if (!Spec || !Spec->IsActive())
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		return;
	}

	FBTBHCrabTaskMemory* Memory = CastInstanceNodeMemory<FBTBHCrabTaskMemory>(NodeMemory);
	Memory->Elapsed += DeltaSeconds;
	if (Memory->Elapsed >= Timeout)
	{
		ASC->CancelAbility(Spec->Ability);
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}
}

EBTNodeResult::Type UBTTask_BH_ActivateAbilityByClass::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	const AAIController* AIOwner = OwnerComp.GetAIOwner();
	UAbilitySystemComponent* ASC = AIOwner ? UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(AIOwner->GetPawn()) : nullptr;
	if (ASC && AbilityClass)
	{
		const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromClass(AbilityClass);
		if (Spec && Spec->IsActive())
		{
			ASC->CancelAbility(Spec->Ability);
		}
	}
	return EBTNodeResult::Aborted;
}

// ============================================================================
// Flank Move
// ============================================================================

UBTTask_BH_FlankMove::UBTTask_BH_FlankMove(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NodeName = TEXT("BH Flank Move");
	INIT_TASK_NODE_NOTIFY_FLAGS();
}

FString UBTTask_BH_FlankMove::GetStaticDescription() const
{
	return FString::Printf(TEXT("Strafe to %.0f cm from target, %.0f deg off the approach line -> %s"), FlankRadius, FlankAngle, *FlankLocationKeyName.ToString());
}

EBTNodeResult::Type UBTTask_BH_FlankMove::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	ABH_CrabAIController* Controller = Cast<ABH_CrabAIController>(OwnerComp.GetAIOwner());
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	const AActor* Target = Controller ? Controller->GetCrabTarget() : nullptr;
	UWorld* World = Pawn ? Pawn->GetWorld() : nullptr;
	UNavigationSystemV1* NavSys = World ? UNavigationSystemV1::GetCurrent(World) : nullptr;
	if (!Pawn || !Target || !NavSys)
	{
		return EBTNodeResult::Failed;
	}

	FBTBHCrabTaskMemory* Memory = CastInstanceNodeMemory<FBTBHCrabTaskMemory>(NodeMemory);
	Memory->Elapsed = 0.f;

	const FVector TargetLoc = Target->GetActorLocation();
	FVector OutDir = (Pawn->GetActorLocation() - TargetLoc).GetSafeNormal2D();
	if (OutDir.IsNearlyZero())
	{
		OutDir = -Target->GetActorForwardVector().GetSafeNormal2D();
	}

	// Try a random side first, then the other one.
	const float FirstSide = FMath::RandBool() ? 1.f : -1.f;
	FNavLocation NavPoint;
	bool bFound = false;
	for (int32 Attempt = 0; Attempt < 2 && !bFound; ++Attempt)
	{
		const float Side = (Attempt == 0) ? FirstSide : -FirstSide;
		const FVector Dir = OutDir.RotateAngleAxis(Side * FlankAngle, FVector::UpVector);
		const FVector Wanted = TargetLoc + Dir * FlankRadius;
		bFound = NavSys->ProjectPointToNavigation(Wanted, NavPoint, FVector(150.f, 150.f, 250.f));
	}
	if (!bFound)
	{
		return EBTNodeResult::Failed;
	}

	if (UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent())
	{
		BB->SetValueAsVector(FlankLocationKeyName, NavPoint.Location);
	}

	FAIMoveRequest MoveRequest(NavPoint.Location);
	MoveRequest.SetAcceptanceRadius(AcceptanceRadius);
	MoveRequest.SetCanStrafe(true);
	MoveRequest.SetUsePathfinding(true);

	const FPathFollowingRequestResult Result = Controller->MoveTo(MoveRequest);
	switch (Result.Code)
	{
	case EPathFollowingRequestResult::AlreadyAtGoal:
		return EBTNodeResult::Succeeded;
	case EPathFollowingRequestResult::RequestSuccessful:
		return EBTNodeResult::InProgress;
	default:
		return EBTNodeResult::Failed;
	}
}

void UBTTask_BH_FlankMove::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	ABH_CrabAIController* Controller = Cast<ABH_CrabAIController>(OwnerComp.GetAIOwner());
	if (!Controller || !Controller->GetCrabTarget())
	{
		if (Controller)
		{
			Controller->StopMovement();
		}
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	FBTBHCrabTaskMemory* Memory = CastInstanceNodeMemory<FBTBHCrabTaskMemory>(NodeMemory);
	Memory->Elapsed += DeltaSeconds;

	if (Controller->GetMoveStatus() == EPathFollowingStatus::Idle)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
	}
	else if (Memory->Elapsed >= MaxMoveTime)
	{
		Controller->StopMovement();
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
	}
}

EBTNodeResult::Type UBTTask_BH_FlankMove::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	if (ABH_CrabAIController* Controller = Cast<ABH_CrabAIController>(OwnerComp.GetAIOwner()))
	{
		Controller->StopMovement();
	}
	return EBTNodeResult::Aborted;
}
