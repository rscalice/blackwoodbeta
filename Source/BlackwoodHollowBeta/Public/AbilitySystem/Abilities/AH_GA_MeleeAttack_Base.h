// Blackwood Hollow - Melee combo attack ability base
// Target: Unreal Engine 5.8 (C++), GAS (GameplayAbilities plugin required)
//
// Create Blueprint children (e.g. GA_SwordAndShield_LightCombo) and set
// AttackMontage + ComboSectionNames there. Drive it from input with
// UBH_CombatFunctionLibrary::HandleMeleeAttackInput, which activates the
// ability on the first press and sends Event.Combat.Input.Attack on later
// presses so they can be buffered into the combo.
//
// Montage authoring requirements:
//   - One montage, one section per combo step (e.g. Attack1, Attack2, Attack3),
//     listed in order in ComboSectionNames. The ability un-links sections at
//     runtime, so the montage ends after the current step unless input advances it.
//   - Each section gets a UANS_ComboWindow (when the next input is accepted)
//     and one or more UANS_MeleeHitbox (when the weapon deals damage).
//   - Use a montage slot the character's AnimGraph actually plays
//     (ABP_SandboxCharacter has "DefaultSlot").
//
// Networking: LocalPredicted. The owning client buffers input and jumps
// sections (ASC montage jumps are forwarded to the server); damage is applied
// only on the authority, from the server's own UANS_MeleeHitbox sweeps.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "AH_GA_MeleeAttack_Base.generated.h"

class UAnimMontage;
class UGameplayEffect;
class UAbilityTask_PlayMontageAndWait;

UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API UAH_GA_MeleeAttack_Base : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UAH_GA_MeleeAttack_Base();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	// -- Montage / combo ---------------------------------------------------------

	/** Montage containing every combo step as its own section. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee|Combo")
	TObjectPtr<UAnimMontage> AttackMontage;

	/** Section names in combo order. Step 0 plays on activation. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee|Combo")
	TArray<FName> ComboSectionNames;

	/** Base montage play rate. The effective rate is MontagePlayRate * the owner's AttackSpeed attribute, sampled at activation (and for recoil). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee|Combo", meta = (ClampMin = "0.1"))
	float MontagePlayRate = 1.f;

	/**
	 * true  = jump to the next step the moment a buffered press meets an open combo window (snappy).
	 * false = wait until the combo window closes, then jump (commits to more of the current swing).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee|Combo")
	bool bAdvanceImmediatelyInWindow = true;

	/** Accept presses made before the combo window opens (held until it opens). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee|Combo")
	bool bBufferInputBeforeWindow = true;

	/** After the last step, a buffered press wraps back to step 0 instead of being ignored. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee|Combo")
	bool bLoopCombo = false;

	// -- Damage ------------------------------------------------------------------

	/** Instant GE that adds SetByCaller(Data.Damage) to UAH_AttributeSet::IncomingDamage. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee|Damage")
	TSubclassOf<UGameplayEffect> DamageEffectClass;

	/** Flat damage per hit before multipliers. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee|Damage", meta = (ClampMin = "0.0"))
	float BaseDamage = 10.f;

	/** Add the attacker's AttackPower attribute to BaseDamage. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee|Damage")
	bool bAddAttackPower = true;

	/** Subtract the target's Defense attribute (result never drops below MinimumDamage). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee|Damage")
	bool bSubtractTargetDefense = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee|Damage", meta = (ClampMin = "0.0"))
	float MinimumDamage = 1.f;

	/** Per-step multiplier, indexed like ComboSectionNames. Missing entries count as 1.0. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee|Damage")
	TArray<float> ComboStepDamageMultipliers;

	/** Instant GE that adds SetByCaller(Data.PostureDamage) to Posture (sent negative). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee|Posture")
	TSubclassOf<UGameplayEffect> PostureDamageEffectClass;

	/** Posture removed from the target per (un-parried) hit, before multipliers. 0 = none. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee|Posture", meta = (ClampMin = "0.0"))
	float BasePostureDamage = 10.f;

	/**
	 * Played on the attacker when the target parries (Event.Combat.Parry.Success):
	 * the attack montage is cut and this recoil plays; the ability ends when it finishes.
	 * Leave empty to keep swinging (only K2_OnAttackParried fires, or the combo ends if bEndComboWhenParried).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee|Parry")
	TObjectPtr<UAnimMontage> RecoilMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee|Parry", meta = (ClampMin = "0.1"))
	float RecoilPlayRate = 1.f;

	/** Only used when RecoilMontage is empty: end (cancel) the combo when parried. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee|Parry")
	bool bEndComboWhenParried = false;

	/**
	 * true = this attack's posture damage is applied even when the target is blocking (guard-breaker,
	 * e.g. shield bash). The block still mitigates the health damage as usual.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee|Posture")
	bool bIgnoreBlockForPosture = false;

	/** Damage AND posture damage multiplier while the attacker has State.Combat.RiposteReady (consumed by the first hit). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee|Damage", meta = (ClampMin = "1.0"))
	float RiposteDamageMultiplier = 2.5f;

	/**
	 * Optional effect applied to the ATTACKER (self) on every successful hit (not parried, damage applied; authority only).
	 * Dual swords set this to UAH_GE_Flurry for attack-speed momentum. Null = none.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Melee|Momentum")
	TSubclassOf<UGameplayEffect> OnHitSelfEffectClass;

	/** Current combo step (0-based). On the server this is derived from the playing montage section. */
	UFUNCTION(BlueprintPure, Category = "Melee|Combo")
	int32 GetCurrentComboStep() const;

protected:
	/** A combo step started playing (including step 0 on activation). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Melee", meta = (DisplayName = "On Combo Step Started"))
	void K2_OnComboStepStarted(int32 ComboStep, FName SectionName);

	/** A hit connected and damage was applied (authority only). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Melee", meta = (DisplayName = "On Hit Confirmed"))
	void K2_OnHitConfirmed(AActor* HitActor, const FHitResult& HitResult, float DamageApplied);

	/** The target parried this attack (Event.Combat.Parry.Success). Play a recoil here. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Melee", meta = (DisplayName = "On Attack Parried"))
	void K2_OnAttackParried(AActor* Parrier);

	/** Override to customise final damage (default: (Base [+AttackPower]) * StepMult * HitboxMult [- Defense]). */
	UFUNCTION(BlueprintNativeEvent, Category = "Melee|Damage")
	float CalculateDamage(AActor* Target, int32 ComboStep, float HitboxMultiplier) const;

private:
	UFUNCTION() void OnMontageCompleted();
	UFUNCTION() void OnMontageBlendOut();
	UFUNCTION() void OnMontageInterrupted();
	UFUNCTION() void OnMontageCancelled();

	UFUNCTION() void OnComboWindowOpened(FGameplayEventData Payload);
	UFUNCTION() void OnComboWindowClosed(FGameplayEventData Payload);
	UFUNCTION() void OnAttackInput(FGameplayEventData Payload);
	UFUNCTION() void OnHitDealt(FGameplayEventData Payload);
	UFUNCTION() void OnParried(FGameplayEventData Payload);

	/** Jumps the montage to the next combo step. Returns false at the end of a non-looping combo. */
	bool AdvanceCombo();

	/** Cuts the attack montage and plays RecoilMontage (parried). */
	void PlayRecoil();

	/** Makes the montage stop at the end of SectionName instead of flowing into the next section. */
	void UnlinkSection(FName SectionName) const;

	float GetStepDamageMultiplier(int32 ComboStep) const;

	/** BaseRate * owner's AttackSpeed attribute (1.0 if the owner has no UAH_AttributeSet). Never below 0.1. */
	float GetEffectivePlayRate(float BaseRate) const;

	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> MontageTask;

	int32 LocalComboStep = 0;
	bool bComboWindowOpen = false;
	bool bInputBuffered = false;

	/** This step's combo window already closed without input: late presses are ignored until the next step. */
	bool bWindowClosedThisStep = false;

	/** Set when we jump mid-window; the stale Close event from the old step's window is skipped. */
	bool bIgnoreNextWindowClose = false;

	/** Recoiling from a parry: combo input, windows and hits are ignored. */
	bool bInRecoil = false;
};
