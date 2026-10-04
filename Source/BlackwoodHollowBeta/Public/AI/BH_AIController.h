// Blackwood Hollow - melee AI brain
// Target: Unreal Engine 5.8 (C++), GAS
//
// A tick-driven state machine (no BehaviorTree) that fights through the same ability pipeline as the player:
//   Idle -> (acquire nearest hostile pawn in AggroRange with line of sight) -> Approach (MoveToActor + SetFocus)
//   -> Attack (a 1-3 swing combo fed through HandleMeleeAttackInput) -> Recover (optional backstep)
//   Defend: reactive -- when the target starts attacking close by, roll parry / block / nothing.
//   Paused: Staggered / PostureBroken / Dead -- movement stopped, focus cleared, resumes by itself.
//   Attack tokens (UBH_AttackTokenSubsystem): before BeginAttack the brain must hold the target's token. Denied -> Circle:
//   a menacing wait at CircleRadiusMin..Max strafing around the target, asking for the token every tick; granted -> Approach
//   (run in) and swing. The token is released when the combo ends, on pause, target change, unpossess and a failed start.
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
	Paused,
	Circle
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

	/**
	 * Margin added to MinAttackDistance to form the lower edge of the "attack band". The approach never stops closer
	 * than MinAttackDistance + this (clamped to AttackRange), so MoveTo drift cannot land the pawn inside MinAttackDistance
	 * and trigger a back-off / re-approach stall. Desired stop = Max(AttackRange - AcceptanceSlack, MinAttackDistance + this).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Brain", meta = (ClampMin = "0"))
	float MinAttackBandMargin = 25.f;

	/**
	 * Inside MinAttackDistance: true = ease back before swinging (the original behaviour); false = hold ground, face the
	 * target and swing if the other attack conditions hold (aggressive / relentless enemies).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Brain")
	bool bBackOffWhenTooClose = true;

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

	/** Minimum seconds between two backsteps (0 = no throttle). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Brain", meta = (ClampMin = "0"))
	float BackstepCooldown = 3.0f;

	/** If true, a combo that ended right after a backstepped combo never backsteps (no back-to-back backsteps). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Brain")
	bool bNoConsecutiveBacksteps = true;

	/** Recovery after a swing that failed to start (stamina / blocked by a state tag). No backstep is rolled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Brain", meta = (ClampMin = "0"))
	float FailedAttackRetryDelay = 0.25f;

	/** 0 = off. While recovering, if the target is farther than AttackRange + this, recovery ends early and the brain re-approaches (pressure). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Brain", meta = (ClampMin = "0"))
	float RecoverChaseDistance = 0.f;

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

	// -- Attack tokens / circling (BH|Brain|Tokens) ---------------------------------------

	/** Ask UBH_AttackTokenSubsystem for the target's attack token before swinging; without it the brain circles. false = swing freely (old behaviour). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Brain|Tokens")
	bool bUseAttackTokens = true;

	/** Circle: closer than this to the target the brain steps back. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Brain|Tokens", meta = (ClampMin = "0"))
	float CircleRadiusMin = 300.f;

	/** Circle: farther than this from the target the brain steps in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Brain|Tokens", meta = (ClampMin = "0"))
	float CircleRadiusMax = 450.f;

	/** Seconds between strafe direction flips (random in the range). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Brain|Tokens", meta = (ClampMin = "0.1"))
	float CircleFlipTimeMin = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Brain|Tokens", meta = (ClampMin = "0.1"))
	float CircleFlipTimeMax = 3.0f;

	/** Chance per strafe flip to also feint: a quick step toward the target and back (no token needed, never an attack). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Brain|Tokens", meta = (ClampMin = "0", ClampMax = "1"))
	float CircleFeintChance = 0.1f;

	/** Total seconds of a feint (half in, half out). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Brain|Tokens", meta = (ClampMin = "0.1"))
	float CircleFeintDuration = 0.7f;

	/** Other AIs within this distance push the circling brain away (anti stacking). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Brain|Tokens", meta = (ClampMin = "0"))
	float CircleSeparationRadius = 200.f;

	/** Strength of that separation push relative to the strafe input (1 = as strong as the strafe). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Brain|Tokens", meta = (ClampMin = "0"))
	float CircleSeparationWeight = 1.2f;

	/** Strafing slower than this (cm/s, after a short grace period) counts as blocked and flips the direction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Brain|Tokens", meta = (ClampMin = "0"))
	float CircleBlockedSpeed = 25.f;

	/** Seconds of blocked strafing before the flip. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Brain|Tokens", meta = (ClampMin = "0.05"))
	float CircleBlockedTime = 0.4f;

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

	/** Swing starts rejected by HandleMeleeAttackInput (stamina / state-tag blocked). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "BH|Brain|Stats")
	int32 StatFailedAttackStarts = 0;

	/** Times the brain was denied an attack token and went to Circle. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "BH|Brain|Stats")
	int32 StatTokenWaits = 0;

	/** Total seconds spent in the Circle state. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "BH|Brain|Stats")
	float StatCircleTime = 0.f;

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
	void TickCircle(float DeltaSeconds);

	/** Asks the token subsystem for the current target's token (true when tokens are off / unavailable). */
	bool AcquireAttackToken();
	/** Gives the current target's token back (safe when none is held). */
	void ReleaseAttackToken();
	void EnterCircle();

	void BeginAttack();
	/** OverrideRecoverTime < 0 = roll RecoverTimeMin..Max. bAllowBackstep = false skips the backstep roll (failed swing starts). */
	void BeginRecover(bool bAllowBackstep = true, float OverrideRecoverTime = -1.f);
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
	float LastBackstepTime = -100000.f;
	bool bLastComboBackstepped = false;

	// Defend
	bool bDefendIsParry = false;
	bool bParryFired = false;
	bool bBlocking = false;
	float ReactionTimeLeft = 0.f;
	float DefendTimeLeft = 0.f;

	// Circle
	float CircleDir = 1.f;
	float CircleFlipTimeLeft = 0.f;
	float CircleBlockedTimer = 0.f;
	float CircleGraceTimeLeft = 0.f;
	float FeintTimeLeft = 0.f;
};
