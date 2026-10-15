// Blackwood Hollow - Parry ability
// Target: Unreal Engine 5.8 (C++), GAS (GameplayAbilities plugin required)
//
// Timed deflect in the Sekiro style. On activation it (optionally) plays
// ParryMontage and, after ParryWindowStartDelay, grants State.Combat.Parrying
// for ParryWindowDuration seconds. While that tag is present:
//   - UAH_AttributeSet throws out any IncomingDamage whose spec carries
//     Damage.Type.Melee (the damage negation), and
//   - this ability listens for Event.Combat.Hit (sent pre-damage by
//     UANS_MeleeHitbox) and runs the posture exchange: the attacker loses
//     posture, the parrier pays a small posture cost, and
//     Event.Combat.Parry.Success is sent to both sides.
//
// The player no longer activates this ability by input: a block raised within UAH_GA_Block::ParryWindow
// of the hit is a perfect block and reuses the outcome below (ExecutePerfectBlockOutcome). Enemy AI still
// activates it through UBH_CombatFunctionLibrary::HandleParryInput.
//
// Activating a parry cancels any running Ability.Combat.MeleeAttack, and
// melee attacks cannot start while it is active.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayTagContainer.h"
#include "AH_GA_Parry.generated.h"

class UAnimMontage;
class UGameplayEffect;
class UAbilityTask_PlayMontageAndWait;
class UAbilitySystemComponent;

UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API UAH_GA_Parry : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UAH_GA_Parry();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	// -- Timing ------------------------------------------------------------------

	/**
	 * Fallback deflect animation (the Sword & Shield parry on GA_SnS_Parry). Used when the current
	 * stance has no entry in StanceParryMontagesByTag. If neither resolves, the ability runs on
	 * RecoveryDuration instead. If a montage resolves, the ability ends when it finishes.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Parry|Timing")
	TObjectPtr<UAnimMontage> ParryMontage;

	/**
	 * Per-stance deflect animation, keyed by Stance.Weapon.* tag (e.g. Stance.Weapon.Greatsword).
	 * Stance is replicated, so a LocalPredicted client and the server resolve the same montage.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Parry|Timing", meta = (Categories = "Stance.Weapon"))
	TMap<FGameplayTag, TObjectPtr<UAnimMontage>> StanceParryMontagesByTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Parry|Timing", meta = (ClampMin = "0.1"))
	float MontagePlayRate = 1.f;

	/** Seconds after activation before the parry frames start. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Parry|Timing", meta = (ClampMin = "0.0"))
	float ParryWindowStartDelay = 0.f;

	/** Length of the active parry frames (State.Combat.Parrying). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Parry|Timing", meta = (ClampMin = "0.01"))
	float ParryWindowDuration = 0.25f;

	/** Only used when ParryMontage is empty: total time before the ability ends. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Parry|Timing", meta = (ClampMin = "0.0"))
	float RecoveryDuration = 0.5f;

	/** End the ability right after a successful parry (lets the player act again immediately). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Parry|Timing")
	bool bEndOnSuccessfulParry = false;

	// -- Posture exchange ---------------------------------------------------------

	/** Instant GE that adds SetByCaller(Data.PostureDamage) to Posture (sent negative). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Parry|Posture")
	TSubclassOf<UGameplayEffect> PostureDamageEffectClass;

	/** Posture removed from the ATTACKER per parried hit, before scaling. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Parry|Posture", meta = (ClampMin = "0.0"))
	float AttackerPostureDamage = 30.f;

	/** Extra attacker posture damage per point of the parrier's AttackPower (0.02 = +2% per point). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Parry|Posture", meta = (ClampMin = "0.0"))
	float AttackPowerPostureScale = 0.02f;

	/** Posture the PARRIER pays per parried hit, before scaling. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Parry|Posture", meta = (ClampMin = "0.0"))
	float DefenderPostureCost = 5.f;

	/** Scale both posture values by the hitbox's DamageMultiplier (heavier attacks = bigger exchange). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Parry|Posture")
	bool bScaleWithHitMultiplier = true;

	/** Applied to the PARRIER (self, authority) on every successful parry: grants State.Combat.RiposteReady for its duration. Null = no riposte. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Parry|Riposte")
	TSubclassOf<UGameplayEffect> RiposteWindowEffectClass;

	UFUNCTION(BlueprintPure, Category = "Parry")
	bool IsParryWindowActive() const { return bParryWindowActive; }

	/**
	 * Perfect block (2026-10-15): the player has no parry key. UAH_GA_Block calls this for a hit that lands
	 * within its ParryWindow. Runs the same outcome as a timed parry (posture exchange, riposte window, the
	 * ParrySuccess cue, Event.Combat.Parry.Success) using the tunables of the first UAH_GA_Parry granted to
	 * ParrierASC. Authority only. Returns false if the parrier has no parry ability to read tunables from.
	 */
	static bool ExecutePerfectBlockOutcome(UAbilitySystemComponent* ParrierASC, AActor* Attacker, const FGameplayEventData& Payload);

	/** The default posture exchange for one parried hit (the pure part of CalculateParryPostureDamage). */
	void ComputePostureExchange(const UAbilitySystemComponent* ParrierASC, float HitMultiplier, float& OutAttackerPostureDamage, float& OutSelfPostureCost) const;

	/** Shared parry outcome: posture deltas, riposte window, ParrySuccess cue (authority) and Parry.Success events (both sides). */
	static void ExecuteParryOutcome(const UAH_GA_Parry* Tunables, UAbilitySystemComponent* ParrierASC, AActor* Attacker,
		const FGameplayEventData& Payload, float PostureToAttacker, float PostureToSelf);

protected:
	UFUNCTION(BlueprintImplementableEvent, Category = "Parry", meta = (DisplayName = "On Parry Window Opened"))
	void K2_OnParryWindowOpened();

	UFUNCTION(BlueprintImplementableEvent, Category = "Parry", meta = (DisplayName = "On Parry Window Closed"))
	void K2_OnParryWindowClosed();

	/** A hit was deflected. Spawn sparks / play the deflect reaction here. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Parry", meta = (DisplayName = "On Parry Success"))
	void K2_OnParrySuccess(AActor* Attacker, float PostureDamageToAttacker, float PostureCostToSelf);

	/**
	 * Posture exchange for one parried hit. Default:
	 *   Attacker = AttackerPostureDamage * (1 + ParrierAttackPower * AttackPowerPostureScale) * HitMultiplier
	 *   Self     = DefenderPostureCost * HitMultiplier
	 * (HitMultiplier is 1 when bScaleWithHitMultiplier is false.)
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "Parry|Posture")
	void CalculateParryPostureDamage(AActor* Attacker, float HitMultiplier, float& OutAttackerPostureDamage, float& OutSelfPostureCost) const;

private:
	UFUNCTION() void OnMontageFinished();
	UFUNCTION() void OnMontageInterrupted();
	UFUNCTION() void OnIncomingHit(FGameplayEventData Payload);

	void OpenParryWindow();
	void CloseParryWindow();
	void FinishParry();

	static void ApplyPostureDelta(const UAH_GA_Parry* Tunables, UAbilitySystemComponent* SourceASC, UAbilitySystemComponent* TargetASC, float PostureDamage);

	/** Montage for the avatar's current stance (StanceParryMontagesByTag), falling back to ParryMontage. May be null. */
	UAnimMontage* ResolveParryMontage() const;

	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> MontageTask;

	FTimerHandle WindowOpenTimerHandle;
	FTimerHandle WindowCloseTimerHandle;
	FTimerHandle RecoveryTimerHandle;

	bool bParryWindowActive = false;
};
