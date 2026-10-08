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
//                                  there while the controller keeps focus on the target. With bOrbit (for crabs waiting on the attack
//                                  token) the point is instead OrbitMinRadius..OrbitMaxRadius from the target, OrbitAngleMin..Max degrees
//                                  round from the current target->crab line (same side as last time 70% of the time).
//   BH Chase Target ............. latent: MoveToActor on the controller's target (no strafe) until within AcceptanceRadius (Succeeded);
//                                  Failed on a failed move, a lost target or after MaxChaseTime. Used by the code-built tree.
//   BH Wait ..................... latent: waits WaitTime +/- RandomDeviation seconds (Succeeded). Used by the code-built tree.

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
	float MaxWaitTime = 0.75f;

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

/** Flank Move memory: the move timer plus the orbit side used last time (+1 / -1, 0 = none yet). */
struct FBTBHFlankMemory
{
	float Elapsed = 0.f;
	int32 LastOrbitSide = 0;
};

UCLASS(meta = (DisplayName = "BH Flank Move"))
class BLACKWOODHOLLOWBETA_API UBTTask_BH_FlankMove : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_BH_FlankMove(const FObjectInitializer& ObjectInitializer);

	virtual uint16 GetInstanceMemorySize() const override { return sizeof(FBTBHFlankMemory); }
	virtual void InitializeMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryInit::Type InitType) const override;
	virtual FString GetStaticDescription() const override;

	/** Distance from the target of the flank point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Task", meta = (ClampMin = "50.0"))
	float FlankRadius = 260.f;

	/** Angle off the target->crab line (degrees); the side is random. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Task", meta = (ClampMin = "10.0", ClampMax = "170.0"))
	float FlankAngle = 90.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Task", meta = (ClampMin = "5.0"))
	float AcceptanceRadius = 60.f;

	/** Gives up (Failed) after this many seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Task", meta = (ClampMin = "0.5"))
	float MaxMoveTime = 4.f;

	/** Blackboard vector key that receives the flank point (the key is optional; a missing key is ignored). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Task")
	FName FlankLocationKeyName = TEXT("FlankLocation");

	/** Orbit mode (for crabs without the attack token): circle the target at OrbitMinRadius..OrbitMaxRadius instead of the single flank point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit")
	bool bOrbit = false;

	/** Orbit radius range (cm). A crab already closer than OrbitMinRadius backs off to OrbitMinRadius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit", meta = (ClampMin = "50.0", EditCondition = "bOrbit"))
	float OrbitMinRadius = 250.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit", meta = (ClampMin = "50.0", EditCondition = "bOrbit"))
	float OrbitMaxRadius = 400.f;

	/** Angle (degrees) the orbit point is rotated round the target from the current target->crab line; picked in this range. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit", meta = (ClampMin = "5.0", ClampMax = "170.0", EditCondition = "bOrbit"))
	float OrbitAngleMin = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit", meta = (ClampMin = "5.0", ClampMax = "170.0", EditCondition = "bOrbit"))
	float OrbitAngleMax = 75.f;

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
};

/** Chase Target memory: seconds spent chasing. */
struct FBTBHChaseMemory
{
	float Elapsed = 0.f;
};

UCLASS(meta = (DisplayName = "BH Chase Target"))
class BLACKWOODHOLLOWBETA_API UBTTask_BH_ChaseTarget : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_BH_ChaseTarget(const FObjectInitializer& ObjectInitializer);

	virtual uint16 GetInstanceMemorySize() const override { return sizeof(FBTBHChaseMemory); }
	virtual FString GetStaticDescription() const override;

	/** Distance (cm) from the target at which the chase counts as arrived. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Task", meta = (ClampMin = "5.0"))
	float AcceptanceRadius = 120.f;

	/** Gives up (Failed) after this many seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Task", meta = (ClampMin = "0.5"))
	float MaxChaseTime = 4.f;

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
};

/** Wait memory: seconds left. */
struct FBTBHWaitMemory
{
	float Remaining = 0.f;
};

UCLASS(meta = (DisplayName = "BH Wait"))
class BLACKWOODHOLLOWBETA_API UBTTask_BH_Wait : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_BH_Wait(const FObjectInitializer& ObjectInitializer);

	virtual uint16 GetInstanceMemorySize() const override { return sizeof(FBTBHWaitMemory); }
	virtual FString GetStaticDescription() const override;

	/** Seconds to wait. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Task", meta = (ClampMin = "0.0"))
	float WaitTime = 0.4f;

	/** The wait is WaitTime plus a random amount in [-RandomDeviation, +RandomDeviation] (never below 0). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Task", meta = (ClampMin = "0.0"))
	float RandomDeviation = 0.f;

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
};
