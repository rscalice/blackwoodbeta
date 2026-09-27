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
// Activating a parry cancels any running Ability.Combat.MeleeAttack, and
// melee attacks cannot start while it is active.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
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

	/** Optional deflect animation. If set, the ability ends when it finishes. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Parry|Timing")
	TObjectPtr<UAnimMontage> ParryMontage;

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

	UFUNCTION(BlueprintPure, Category = "Parry")
	bool IsParryWindowActive() const { return bParryWindowActive; }

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

	void ApplyPostureDelta(UAbilitySystemComponent* TargetASC, float PostureDamage) const;

	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> MontageTask;

	FTimerHandle WindowOpenTimerHandle;
	FTimerHandle WindowCloseTimerHandle;
	FTimerHandle RecoveryTimerHandle;

	bool bParryWindowActive = false;
};
