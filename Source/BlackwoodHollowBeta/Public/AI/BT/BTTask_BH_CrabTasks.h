// Blackwood Hollow - crab Behavior Tree tasks
// Target: Unreal Engine 5.8 (C++)
//
//   BH Request Attack Token ..... latent: asks the crab controller for the target's attack token every tick until granted
//                                  (Succeeded) or MaxWaitTime runs out (Failed, the waiter entry is dropped). Instant success when
//                                  tokens are disabled.
//   BH Release Attack Token ..... instant: gives the token back (starts the target's handoff cooldown).
//   BH Activate Ability By Class  latent: TryActivateAbilityByClass on the crab's ASC; optionally waits until the ability ends
//                                  (Timeout guards against a stuck ability and cancels it). Aborting the task cancels the ability.
//   BH Flank Move ............... picks a point FlankRadius from the target, 90 degrees off the target->crab line (random side,
//                                  projected on the nav mesh), writes it to the blackboard (FlankLocationKeyName) and strafe-moves
//                                  there while the controller keeps focus on the target.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_BH_CrabTasks.generated.h"

class UGameplayAbility;

struct FBTBHCrabTaskMemory
{
	float Elapsed = 0.f;
};

UCLASS(meta = (DisplayName = "BH Request Attack Token"))
class BLACKWOODHOLLOWBETA_API UBTTask_BH_RequestAttackToken : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_BH_RequestAttackToken(const FObjectInitializer& ObjectInitializer);

	virtual uint16 GetInstanceMemorySize() const override { return sizeof(FBTBHCrabTaskMemory); }
	virtual FString GetStaticDescription() const override;

	/** Seconds to wait for the token before failing (0 = single try). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Task", meta = (ClampMin = "0.0"))
	float MaxWaitTime = 3.f;

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
};

UCLASS(meta = (DisplayName = "BH Release Attack Token"))
class BLACKWOODHOLLOWBETA_API UBTTask_BH_ReleaseAttackToken : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_BH_ReleaseAttackToken(const FObjectInitializer& ObjectInitializer);
	virtual FString GetStaticDescription() const override;

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
};

UCLASS(meta = (DisplayName = "BH Activate Ability By Class"))
class BLACKWOODHOLLOWBETA_API UBTTask_BH_ActivateAbilityByClass : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_BH_ActivateAbilityByClass(const FObjectInitializer& ObjectInitializer);

	virtual uint16 GetInstanceMemorySize() const override { return sizeof(FBTBHCrabTaskMemory); }
	virtual FString GetStaticDescription() const override;

	/** Ability to activate (must be granted to the crab). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Task")
	TSubclassOf<UGameplayAbility> AbilityClass;

	/** true: the task stays in progress until the ability ends. false: succeeds right after a successful activation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Task")
	bool bWaitForCompletion = true;

	/** Seconds before a still-running ability is cancelled and the task fails. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Task", meta = (ClampMin = "0.1", EditCondition = "bWaitForCompletion"))
	float Timeout = 4.f;

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
};

UCLASS(meta = (DisplayName = "BH Flank Move"))
class BLACKWOODHOLLOWBETA_API UBTTask_BH_FlankMove : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_BH_FlankMove(const FObjectInitializer& ObjectInitializer);

	virtual uint16 GetInstanceMemorySize() const override { return sizeof(FBTBHCrabTaskMemory); }
	virtual FString GetStaticDescription() const override;

	/** Distance from the target of the flank point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Task", meta = (ClampMin = "50.0"))
	float FlankRadius = 300.f;

	/** Angle off the target->crab line (degrees); the side is random. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Task", meta = (ClampMin = "10.0", ClampMax = "170.0"))
	float FlankAngle = 90.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Task", meta = (ClampMin = "5.0"))
	float AcceptanceRadius = 50.f;

	/** Gives up (Failed) after this many seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Task", meta = (ClampMin = "0.5"))
	float MaxMoveTime = 3.f;

	/** Blackboard vector key that receives the flank point (the key is optional; a missing key is ignored). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Task")
	FName FlankLocationKeyName = TEXT("FlankLocation");

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
};
