// Blackwood Hollow - Hold-to-block ability
// Target: Unreal Engine 5.8 (C++), GAS (GameplayAbilities plugin required)
//
// Active while the block button is held (UBH_CombatFunctionLibrary::HandleBlockInput
// activates on press, cancels on release). Grants State.Combat.Blocking.
//
// Damage mitigation happens in UAH_AttributeSet::PreGameplayEffectExecute, which
// looks up the active block (FindActiveBlock) and uses its tunables:
//   - hits from inside BlockAngleDegrees (frontal arc) lose DamageReduction of
//     their damage,
//   - the blocker's Posture is reduced by (incoming damage * PostureDamageScale),
//   - Event.Combat.BlockImpact is sent instead of Event.Combat.DamageReceived,
//     so no hit reaction plays; this ability answers it with the "Impact"
//     section of GuardMontage.
//
// GuardMontage sections (all optional, looked up by name):
//   "Start" -> raise guard, flows into "Loop"
//   "Loop"  -> held guard (loops on itself)
//   "Impact"-> shield hit, flows back into "Loop"
//   "End"   -> lower guard, played when the block is released

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/AH_GA_StaminaBase.h"
#include "AH_GA_Block.generated.h"

class UAnimMontage;
class UAbilitySystemComponent;
class UAbilityTask_PlayMontageAndWait;

UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API UAH_GA_Block : public UAH_GA_StaminaBase
{
	GENERATED_BODY()

public:
	UAH_GA_Block();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	/** Guard montage with Start / Loop / Impact / End sections. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Block")
	TObjectPtr<UAnimMontage> GuardMontage;

	/** Stance (GASP OverlayPose display name, e.g. "Greatsword") -> guard montage. Falls back to GuardMontage. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Block")
	TMap<FName, TObjectPtr<UAnimMontage>> StanceGuardMontages;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Block", meta = (ClampMin = "0.1"))
	float MontagePlayRate = 1.f;

	/** Fraction of incoming damage removed by a successful block (1 = no chip damage). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Block", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DamageReduction = 0.8f;

	/** Blocker's Posture loss = incoming (pre-mitigation) damage * this. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Block", meta = (ClampMin = "0.0"))
	float PostureDamageScale = 0.5f;

	/**
	 * Stamina drained from the blocker per blocked hit = the hit's posture cost (Event.Combat.BlockImpact magnitude) * this.
	 * Raising the guard itself is free (StaminaCost defaults to 0). When a block impact drains Stamina to 0 the guard
	 * breaks: the remaining Posture is removed in the same hit (-> the normal posture break) and the block ends.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Block|Stamina", meta = (ClampMin = "0.0"))
	float BlockStaminaScale = 1.f;

	/** Width of the frontal arc that can be blocked (180 = anything in front). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Block", meta = (ClampMin = "0.0", ClampMax = "360.0"))
	float BlockAngleDegrees = 120.f;

	/** True if an attack from Attacker's position falls inside this block's arc. */
	UFUNCTION(BlueprintPure, Category = "Block")
	bool IsAttackInBlockArc(const AActor* Attacker) const;

	/** The currently active block ability instance on ASC, or nullptr. */
	static UAH_GA_Block* FindActiveBlock(UAbilitySystemComponent* ASC);

protected:
	/** A hit was blocked. PostureCost is what the block took off Posture. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Block", meta = (DisplayName = "On Block Impact"))
	void K2_OnBlockImpact(AActor* Attacker, float PostureCost);

private:
	UFUNCTION() void OnMontageInterrupted();
	UFUNCTION() void OnBlockImpact(FGameplayEventData Payload);

	/** Block impact: spend stamina (PostureCost * BlockStaminaScale); on empty, break posture and end the block. */
	void DrainStaminaForBlock(float PostureCost);

	bool MontageHasSection(FName SectionName) const;
	void SetSectionLink(FName From, FName To) const;

	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> MontageTask;

	/** The guard montage chosen for the current activation (stance override or GuardMontage). */
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveGuardMontage;
};
