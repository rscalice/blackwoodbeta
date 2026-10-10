// Blackwood Hollow - crab AI controller (Behavior Tree driven)
// Target: Unreal Engine 5.8 (C++), GAS
//
// Server only. Runs the pawn's ABH_EnemyCrab::CrabBehaviorTree (else DefaultBehaviorTree below), finds and keeps a target
// (aggro / lose range, line of sight), focuses it, mirrors a few facts into the Blackboard at ~10 Hz and brokers the attack token
// (UBH_AttackTokenSubsystem) for the BT tasks. It never attacks by itself: all decisions live in the Behavior Tree.
// With bUseCodeBuiltTree (default) the tree is assembled in C++ at runtime (BuildCrabTree) because the BT_BH_Crab asset is corrupted
// and cannot be authored in this build; the asset path (CrabBehaviorTree / DefaultBehaviorTree) is only used when it is off.
//
// BLACKBOARD KEYS (create these in BB_BH_Crab; names are the constants in BH_CrabBB below)
//   TargetActor ........ Object (Actor)   current target, cleared when lost
//   TargetLocation ..... Vector           target's last known location
//   DistanceToTarget ... Float            2D distance to the target (3.4e38 without a target)
//   bTargetGuarding .... Bool             target is Blocking or Parrying
//   bTargetStartedCombo  Bool             target started an attack within TargetComboWindow seconds
//   FlankLocation ...... Vector           written by UBTTask_BH_FlankMove
//   bHasToken .......... Bool             this crab holds the target's attack token
//
// BT NODES (Public/AI/BT): decorators TargetIsGuarding / TargetStartedCombo / SidestepGate, tasks RequestAttackToken /
// ReleaseAttackToken / ActivateAbilityByClass / FlankMove.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "GameplayTagContainer.h"
#include "TimerManager.h"
#include "UObject/SoftObjectPtr.h"
#include "BH_CrabAIController.generated.h"

class UAbilitySystemComponent;
class UBehaviorTree;
class UBlackboardData;

namespace BH_CrabBB
{
	inline const FName TargetActor(TEXT("TargetActor"));
	inline const FName TargetLocation(TEXT("TargetLocation"));
	inline const FName DistanceToTarget(TEXT("DistanceToTarget"));
	inline const FName bTargetGuarding(TEXT("bTargetGuarding"));
	inline const FName bTargetStartedCombo(TEXT("bTargetStartedCombo"));
	inline const FName FlankLocation(TEXT("FlankLocation"));
	inline const FName bHasToken(TEXT("bHasToken"));
}

UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API ABH_CrabAIController : public AAIController
{
	GENERATED_BODY()

public:
	ABH_CrabAIController();

	// -- Tunables (BH|CrabAI) -------------------------------------------------------------

	/** Behavior tree used when the possessed crab has no CrabBehaviorTree set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|CrabAI")
	TObjectPtr<UBehaviorTree> DefaultBehaviorTree;

	/** Hostile pawns closer than this (and visible) become targets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|CrabAI", meta = (ClampMin = "0"))
	float AggroRange = 1800.f;

	/** A target farther than this is dropped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|CrabAI", meta = (ClampMin = "0"))
	float LoseTargetRange = 2600.f;

	/** Seconds between target scans. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|CrabAI", meta = (ClampMin = "0.05"))
	float ScanInterval = 0.3f;

	/** Seconds between Blackboard refreshes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|CrabAI", meta = (ClampMin = "0.02"))
	float BlackboardInterval = 0.1f;

	/** bTargetStartedCombo stays true this long after the target begins an attack. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|CrabAI", meta = (ClampMin = "0"))
	float TargetComboWindow = 0.4f;

	/** false: RequestAttackToken always succeeds (swarm throttling off). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|CrabAI")
	bool bUseAttackTokens = true;

	/** true: StartBrain builds the crab's Behavior Tree in code (BuildCrabTree) and ignores the BT assets. false: run CrabBehaviorTree / DefaultBehaviorTree. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
	bool bUseCodeBuiltTree = true;

	/** Blackboard asset the code-built tree runs on (needs the keys listed in BH_CrabBB). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
	TSoftObjectPtr<UBlackboardData> CodeTreeBlackboard;

	// -- Queries ----------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "BH|CrabAI")
	AActor* GetCrabTarget() const { return CrabTarget.Get(); }

	/** True when the target is Blocking or Parrying. */
	UFUNCTION(BlueprintPure, Category = "BH|CrabAI")
	bool IsTargetGuarding() const;

	/** Seconds since the target's latest attack start (TNumericLimits<float>::Max() if it never attacked while targeted). */
	UFUNCTION(BlueprintPure, Category = "BH|CrabAI")
	float GetSecondsSinceTargetStartedCombo() const;

	UFUNCTION(BlueprintPure, Category = "BH|CrabAI")
	float GetDistanceToCrabTarget() const;

	// -- Attack token (server) ---------------------------------------------------------------

	/** True when this controller currently holds the target's token. */
	bool HasAttackToken() const;

	/** Asks for the token; true = held (or tokens disabled / nothing to ask for). A denied call registers this crab as a waiter. */
	bool RequestAttackToken();

	/** Gives the token back (safe when not holding). */
	void ReleaseAttackToken();

	/** Switches target (null clears). Releases the old target's token. */
	void SetCrabTarget(AActor* NewTarget);

	/** SERVER (Phase 11F party-wipe reset). Drops the target and token, stops moving and restarts the behavior tree from its root. */
	void ResetAI();

	/** SERVER. true = ResetAI, then no target scans until false (the reset component walks the pawn home). */
	void SetResetHold(bool bHold);

	UFUNCTION(BlueprintPure, Category = "BH|CrabAI")
	bool IsResetHeld() const { return bResetHold; }

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

private:
	void ScanForTarget();
	void UpdateBlackboard();
	bool IsValidHostile(const APawn* Candidate, float MaxRange) const;
	UAbilitySystemComponent* GetPawnASC() const;

	void StartBrain();

	/** Builds (once per controller) the crab Behavior Tree from node objects: no asset, no editor graph. Null when the blackboard cannot be loaded. */
	UBehaviorTree* BuildCrabTree();

	/** Logs (once per controller) when a tree's root children have an empty composite entry or a null decorator. */
	void ValidateBehaviorTree(const UBehaviorTree* Tree);
	void OnTargetAttackingChanged(const FGameplayTag Tag, int32 NewCount);

	TWeakObjectPtr<AActor> CrabTarget;
	TWeakObjectPtr<UAbilitySystemComponent> WatchedTargetASC;
	FDelegateHandle TargetAttackingHandle;

	/** World time of the target's latest attack start; negative = never. */
	double LastTargetAttackStartTime = -1.0;

	bool bHoldingToken = false;

	/** Party-wipe reset in progress: ScanForTarget does nothing. */
	bool bResetHold = false;

	/** The corrupted-tree error has been logged for this controller. */
	bool bLoggedCorruptTree = false;

	/** The tree BuildCrabTree assembled (kept alive here; the Behavior Tree manager caches its template by this object). */
	UPROPERTY(Transient)
	TObjectPtr<UBehaviorTree> CodeBuiltTree;

	FTimerHandle ScanTimerHandle;
	FTimerHandle BlackboardTimerHandle;
};
