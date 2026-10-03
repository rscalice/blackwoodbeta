// Blackwood Hollow - melee AI brain
// Target: Unreal Engine 5.8 (C++), GAS
//
// A tick-driven state machine (no BehaviorTree) that fights through the same ability pipeline as the player:
//   Idle -> (acquire nearest hostile pawn in AggroRange with line of sight) -> Approach (MoveToActor + SetFocus)
//   -> Attack (a 1-3 swing combo fed through HandleMeleeAttackInput) -> Recover (optional backstep)
//   Defend: reactive -- when the target starts attacking close by, roll parry / block / nothing.
//   Paused: Staggered / PostureBroken / Dead -- movement stopped, focus cleared, resumes by itself.
// Server only (AI controllers do not exist on clients). Abilities are looked up by their asset tags
// (Ability.Combat.MeleeAttack / Block / Parry / Dodge) among those granted to the pawn, so any BP ability works.
// Team comes from the pawn's UBH_CombatIdentityComponent at possession.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "GameplayTagContainer.h"
#include "BH_AIController.generated.h"

class UAbilitySystemComponent;
class UGameplayAbility;
class UAH_GA_MeleeAttack_Base;

UENUM(BlueprintType)
enum class EBH_AIState : uint8
{
	Idle,
	Approach,
	Attack,
	Recover,
	Defend,
	Paused
};

UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API ABH_AIController : public AAIController
{
	GENERATED_BODY()

public:
	ABH_AIController();

	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintPure, Category = "BH|Brain")
	EBH_AIState GetBrainState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "BH|Brain")
	AActor* GetBrainTarget() const { return Target.Get(); }

	// -- Tunables (BH|Brain) -----------------------------------------------------------

	/** Hostile pawns closer than this (and visible) become targets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Brain", meta = (ClampMin = "0"))
	float AggroRange = 2000.f;

	/** A current target farther than this is dropped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Brain", meta = (ClampMin = "0"))
	float LoseTargetRange = 2800.f;

	/** Distance (centre to centre) at which swings can start. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Brain", meta = (ClampMin = "0"))
	float AttackRange = 140.f;

	/** Approach stops this much inside AttackRange (MoveTo acceptance radius = AttackRange - slack). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Brain", meta = (ClampMin = "0"))
	float AcceptanceSlack = 20.f;

	/** Closer than this (centre to centre) the brain backs off before swinging: the blade sweeps whiff when the target is inside the arc. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Brain", meta = (ClampMin = "0"))
	float MinAttackDistance = 125.f;

	/** A swing needs the target within this many degrees of the pawn's forward. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Brain", meta = (ClampMin = "1", ClampMax = "180"))
	float AttackFacingAngle = 30.f;

	/** Rotate the pawn toward the target by hand when the engine's controller-desired rotation is too slow (deg/s, 0 = off). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Brain", meta = (ClampMin = "0"))
	float AssistTurnSpeed = 360.f;

	/** Inside AttackRange + this the approach walks (gait Walk) instead of running. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Brain", meta = (ClampMin = "0"))
	float WalkInDistance = 150.f;

	/** Farther than this from the target the approach sprints. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Brain", meta = (ClampMin = "0"))
	float SprintDistance = 1200.f;

	/** Relative weights for 1 / 2 / 3 swing combos. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Brain", meta = (ClampMin = "0"))
	float ComboWeight1 = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Brain", meta = (ClampMin = "0"))
	float ComboWeight2 = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Brain", meta = (ClampMin = "0"))
	float ComboWeight3 = 30.f;

	/** Seconds between buffered attack presses while a combo is running. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Brain", meta = (ClampMin = "0.05"))
	float SwingFeedInterval = 0.25f;

	/** Below this stamina the brain will not start or continue a swing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Brain", meta = (ClampMin = "0"))
	float MinStaminaToSwing = 12.f;

	/** Delay after first entering attack range (and after re-acquiring a target) before the first swing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Brain", meta = (ClampMin = "0"))
	float AggressionDelay = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Brain", meta = (ClampMin = "0"))
	float RecoverTimeMin = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Brain", meta = (ClampMin = "0"))
	float RecoverTimeMax = 1.2f;

	/** Chance to backstep (dodge with no movement input) when a combo ends. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Brain", meta = (ClampMin = "0", ClampMax = "1"))
	float BackstepChance = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Brain", meta = (ClampMin = "0"))
	float BackstepMinStamina = 40.f;

	/** Chance to try a parry when the target starts attacking within AttackRange + DefendRangeExtra. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Brain", meta = (ClampMin = "0", ClampMax = "1"))
	float ParryChance = 0.2f;

	/** Chance (partitioned after the parry roll: parry, then block, else nothing) to raise the guard. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Brain", meta = (ClampMin = "0", ClampMax = "1"))
	float BlockChance = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Brain", meta = (ClampMin = "0"))
	float DefendRangeExtra = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Brain", meta = (ClampMin = "0"))
	float ReactionDelayMin = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Brain", meta = (ClampMin = "0"))
	float ReactionDelayMax = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Brain", meta = (ClampMin = "0"))
	float BlockHoldTime = 0.8f;

	/** Minimum seconds between two defensive rolls. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Brain", meta = (ClampMin = "0"))
	float DefendCooldown = 1.0f;

	/** Seconds between target scans. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Brain", meta = (ClampMin = "0.05"))
	float TargetScanInterval = 0.3f;

	/** If MoveTo finds no path (no navmesh), walk straight at the target instead. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Brain")
	bool bDirectMoveFallback = true;

	// -- Debug counters (this session) ---------------------------------------------------

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "BH|Brain|Stats")
	int32 StatCombosStarted = 0;

	/** Combo steps (swings) actually started: the first activation plus every advance the ability made. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "BH|Brain|Stats")
	int32 StatComboSteps = 0;

	/** Swing presses fed to the ability (first activation + buffered presses). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "BH|Brain|Stats")
	int32 StatSwingsFed = 0;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "BH|Brain|Stats")
	int32 StatParriesTried = 0;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "BH|Brain|Stats")
	int32 StatBlocksTried = 0;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "BH|Brain|Stats")
	int32 StatBacksteps = 0;

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

private:
	void SetState(EBH_AIState NewState);
	void ScanForTarget();
	bool IsValidHostile(const APawn* Candidate, float MaxRange) const;
	void SetTarget(AActor* NewTarget);
	void OnTargetAttackingChanged(const FGameplayTag Tag, int32 NewCount);
	void EnterPaused();

	void TickApproach(float DeltaSeconds);
	void TickAttack(float DeltaSeconds);
	void TickRecover(float DeltaSeconds);
	void TickDefend(float DeltaSeconds);

	void BeginAttack();
	void BeginRecover();
	void BeginDefend(bool bParry);
	void EndDefend();
	void FaceTarget(float DeltaSeconds, float MinAngle);
	float GetAngleToTarget() const;
	float GetDistanceToTarget() const;
	float GetStamina() const;

	UClass* FindGrantedAbilityClass(const FGameplayTag& AbilityTag) const;
	UAbilitySystemComponent* GetPawnASC() const;
	bool IsAbilityActive(UClass* AbilityClass) const;
	int32 RollComboLength() const;

	EBH_AIState State = EBH_AIState::Idle;
	TWeakObjectPtr<AActor> Target;

	TWeakObjectPtr<UAbilitySystemComponent> WatchedTargetASC;
	FDelegateHandle TargetAttackingHandle;

	float ScanTimer = 0.f;
	float MoveReissueTimer = 0.f;
	float StateTime = 0.f;
	float DefendCooldownLeft = 0.f;
	float AggressionTimeLeft = 0.f;

	// Attack
	int32 ComboLength = 0;
	float FeedTimer = 0.f;
	bool bAttackStarted = false;
	int32 LastObservedStep = 0;

	// Recover
	float RecoverTimeLeft = 0.f;

	// Defend
	bool bDefendIsParry = false;
	bool bParryFired = false;
	bool bBlocking = false;
	float ReactionTimeLeft = 0.f;
	float DefendTimeLeft = 0.f;
};
