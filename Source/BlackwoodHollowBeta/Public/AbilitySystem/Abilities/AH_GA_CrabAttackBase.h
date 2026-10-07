// Blackwood Hollow - crab ability base (procedural attacks, no montage)
// Target: Unreal Engine 5.8 (C++), GAS
//
// The crab fights with timed, code-driven abilities (poses come from the ABP through ABH_EnemyCrab::ActionPhase) that still go
// through the normal melee damage pipeline, step for step like UAH_GA_MeleeAttack_Base::OnHitDealt:
//   1. Event.Combat.Hit to the victim first (a parry reacts to it),
//   2. parried -> stop (the parry ability handles posture),
//   3. blocked? (target holds a block and the attacker is inside its arc),
//   4. Damage GE (tagged Damage.Type.Melee; UAH_GE_Damage_Formula with SetByCaller Data.Damage / Data.DamageMultiplier /
//      Data.AttackPowerScale) and, unless blocked, the Posture GE,
//   5. tiered pushback and the GameplayCue.Combat.Hit cue (hit-stop / shake).
//
// Server only (NetExecutionPolicy ServerOnly, instanced per actor). The AI starts the abilities from the Behavior Tree
// (BTTask_BH_ActivateAbilityByClass). While tokens are enabled an attack needs the crab's attack token (bRequireAttackToken).
//
// Subclasses implement BeginAttack() and use ScheduleStep() for their timeline; EndAbility cleans up timers, the lunge, the
// telegraph and the replicated phase.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayTagContainer.h"
#include "TimerManager.h"
#include "Characters/BH_CrabTypes.h"
#include "AH_GA_CrabAttackBase.generated.h"

class ABH_EnemyCrab;
class UGameplayEffect;
class UAbilitySystemComponent;

/** Outcome of one ResolveHitOnActor call. */
struct FBH_CrabHitOutcome
{
	bool bConnected = false;   // damage was applied (not skipped / parried)
	bool bParried = false;
	bool bBlocked = false;
	float DamageApplied = 0.f;
};

UCLASS(Abstract, Blueprintable)
class BLACKWOODHOLLOWBETA_API UAH_GA_CrabAttackBase : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UAH_GA_CrabAttackBase();

	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const override;

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	// -- Damage (BH|Crab|Damage) ---------------------------------------------------------------

	/** Flat damage per hit before AttackPower and the multiplier (Data.Damage of the damage formula). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Crab|Damage", meta = (ClampMin = "0.0"))
	float BaseDamage = 4.f;

	/** Per-attack damage multiplier (Data.DamageMultiplier), on top of the crab's combat identity multiplier. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Crab|Damage", meta = (ClampMin = "0.0"))
	float AttackDamageMultiplier = 1.f;

	/** Add the crab's AttackPower to BaseDamage (Data.AttackPowerScale 1, else 0). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Crab|Damage")
	bool bAddAttackPower = true;

	/** No longer used (Phase 9): Defense is always applied by the damage formula, as a divisor. Kept so existing Blueprints still load. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Crab|Damage")
	bool bSubtractTargetDefense = true;

	/** No longer used (Phase 9): the damage formula floors every hit at 1. Kept so existing Blueprints still load. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Crab|Damage", meta = (ClampMin = "0.0"))
	float MinimumDamage = 1.f;

	/** Posture damage to the target per hit (skipped when the hit is blocked, unless bIgnoreBlockForPosture). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Crab|Damage", meta = (ClampMin = "0.0"))
	float BasePostureDamage = 20.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Crab|Damage")
	bool bIgnoreBlockForPosture = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Crab|Damage")
	TSubclassOf<UGameplayEffect> DamageEffectClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Crab|Damage")
	TSubclassOf<UGameplayEffect> PostureDamageEffectClass;

	/** Cue executed on every connected hit (hit-stop, shake, impact FX). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Crab|Damage", meta = (Categories = "GameplayCue"))
	FGameplayTag HitCueTag;

	// -- Behaviour (BH|Crab) --------------------------------------------------------------------

	/** Must hold the target's attack token (UBH_AttackTokenSubsystem) to start. Only enforced for an AI-controlled crab while tokens are enabled. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Crab")
	bool bRequireAttackToken = true;

	/** Snap to face the target when the attack starts. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Crab")
	bool bFaceTargetOnStart = true;

	/** Seconds of recovery after the last hit window before the ability ends. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Crab", meta = (ClampMin = "0.0"))
	float RecoveryTime = 0.35f;

protected:
	/** Subclass entry point (server, after commit). Call FinishAttack() when the whole attack is over. */
	virtual void BeginAttack() {}

	/** Hit-scan hook, called every HitScanInterval while a window started with StartHitScan is open. */
	virtual void HitScanTick() {}

	/** Hit-scan window closed (time up or parried). */
	virtual void OnHitScanFinished() {}

	/** Called after a victim was processed by ResolveHitOnActor (connected, blocked or parried). */
	virtual void OnVictimResolved(AActor* Victim, const FBH_CrabHitOutcome& Outcome) {}

	// -- Helpers --------------------------------------------------------------------------------

	ABH_EnemyCrab* GetCrab() const;

	/** The crab AI controller's target, else the nearest living hostile pawn within 2500 cm, else null. */
	AActor* ResolveTarget() const;

	/** Reports the animation phase (replicated through the crab). */
	void SetPhase(EBH_CrabActionPhase Phase) const;

	/** Rotates the crab's yaw to look at Location now. */
	void FaceLocationNow(const FVector& Location) const;

	/** Ground point under an actor (capsule bottom for characters). */
	static FVector GetGroundPoint(const AActor* Actor);

	/** Pawns overlapping a sphere (object type Pawn), excluding the avatar. */
	void CollectPawnsInSphere(const FVector& Center, float Radius, TArray<AActor*>& OutActors) const;

	/** Full damage / posture / pushback / cue pipeline for one victim. Server. Each victim only once per activation unless bAllowRepeat. */
	FBH_CrabHitOutcome ResolveHitOnActor(AActor* Victim, const FVector& ImpactPoint, bool bAllowRepeat = false);

	/** Data.DamageMultiplier of this attack: AttackDamageMultiplier * the crab's combat identity multiplier. */
	float GetDamageMultiplier() const;

	/** Final damage for Target: UBH_CombatFunctionLibrary::ComputeDamage with the inputs the damage GE gets (for hit-feel tiers). */
	float CalculateDamage(const AActor* Target) const;

	/** Horizontal lunge: moves the crab Distance cm along Direction over Duration seconds (override root motion, ends at rest). */
	void StartLunge(const FVector& Direction, float Distance, float Duration);
	void StopLunge();

	/** Opens a hit window: HitScanTick() runs every Interval seconds for Duration seconds (first tick immediately). */
	void StartHitScan(float Duration, float Interval = 0.03f);
	void StopHitScan();

	/** One-shot step timer (single slot: scheduling replaces the pending step). */
	template <typename UserClass>
	void ScheduleStep(UserClass* Object, void (UserClass::*Method)(), float Delay)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(StepTimer, Object, Method, FMath::Max(Delay, 0.01f), false);
		}
	}
	void CancelStep();

	/** Ends the ability after RecoveryTime. */
	void FinishAttack();

	/** True once per activation when a victim was parried (the window closes and the attack goes straight to recovery). */
	bool WasParried() const { return bParried; }

private:
	void HitScanTickInternal();
	void FinishAttackNow();

	TArray<TWeakObjectPtr<AActor>> HitActors;
	FTimerHandle StepTimer;
	FTimerHandle HitScanTimer;
	FTimerHandle RecoveryTimer;
	double HitScanEndTime = 0.0;
	bool bParried = false;
	bool bFinishing = false;
	FName LungeSourceName;
};
