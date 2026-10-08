// Blackwood Hollow - Blight meter effects: saturation damage + Blight Rot (Phase 10B)
// Target: Unreal Engine 5.8 (C++), GAS (GameplayAbilities plugin required)
//
// Native GameplayEffects used by the Blight build-up meter (UBPC_HeartFragment::AddBlightBuildup):
//
//   UAH_GE_BlightSaturationDamage  Instant. IncomingDamage += SetByCaller(Data.Blight.SaturationPercent) * target MaxHealth
//                                  (UAH_MMC_BlightSaturation). Goes through UAH_AttributeSet's normal IncomingDamage -> Health ->
//                                  death path. Tagged Damage.Type.Blight: not blockable, no parry / dodge negation, and no
//                                  automatic Event.Combat.DamageReceived (the meter sends that itself to stagger the victim).
//   UAH_GE_BlightRot               Status effect (UBH_GE_StatusEffect, grants State.Status.BlightRot). HasDuration (10 s), periodic 1 s;
//                                  every tick IncomingDamage += SetByCaller(Data.Blight.RotDamagePercent) * target MaxHealth
//                                  (UAH_MMC_BlightRotTick, 2.5% by default). Damage.Type.Blight like above. Re-application refreshes
//                                  the duration (one stack).
//   UAH_GE_BlightRotStaminaPenalty Companion of the Rot: while Blight Rot is on, StaminaRegenRate is multiplied by
//                                  SetByCaller(Data.Blight.RotStaminaRegenMult) (0.75 = -25%) -- MultiplyAdditive, so it stacks
//                                  additively with the armor-weight multiplier (UAH_GE_ArmorWeight) and vanishes cleanly with the
//                                  effect. It is a separate effect because a periodic effect's modifiers are executed per tick instead
//                                  of being held as aggregator mods. Apply/remove both through ApplyBlightRot / RemoveBlightRot.
//
// All three are server-applied; the status tag and the attribute changes replicate through the normal GAS paths.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/StatusEffects/BH_StatusEffect.h"
#include "GameplayEffect.h"
#include "GameplayEffectTypes.h"
#include "GameplayModMagnitudeCalculation.h"
#include "BH_BlightEffects.generated.h"

class UAbilitySystemComponent;

/** Magnitude of one Blight Rot tick: SetByCaller(Data.Blight.RotDamagePercent) * the target's live MaxHealth. */
UCLASS()
class BLACKWOODHOLLOWBETA_API UAH_MMC_BlightRotTick : public UGameplayModMagnitudeCalculation
{
	GENERATED_BODY()

public:
	UAH_MMC_BlightRotTick();
	virtual float CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const override;

private:
	FGameplayEffectAttributeCaptureDefinition MaxHealthDef;
};

/** Magnitude of the saturation hit: SetByCaller(Data.Blight.SaturationPercent) * the target's live MaxHealth. */
UCLASS()
class BLACKWOODHOLLOWBETA_API UAH_MMC_BlightSaturation : public UGameplayModMagnitudeCalculation
{
	GENERATED_BODY()

public:
	UAH_MMC_BlightSaturation();
	virtual float CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const override;

private:
	FGameplayEffectAttributeCaptureDefinition MaxHealthDef;
};

/** Instant saturation damage (15% MaxHealth by default). Use ApplySaturationDamage() to apply it. */
UCLASS()
class BLACKWOODHOLLOWBETA_API UAH_GE_BlightSaturationDamage : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UAH_GE_BlightSaturationDamage();

	/** Default fraction of MaxHealth dealt when the meter saturates (SetByCaller Data.Blight.SaturationPercent falls back to it). */
	static constexpr float DefaultSaturationPercent = 0.15f;

	/**
	 * Applies the saturation damage to TargetASC (authority only). SourceASC may be null: the target is then its own source.
	 * InstigatorActor becomes the effect's instigator (kill credit / hit-reaction direction); null = the target's avatar.
	 * @return the damage dealt (SaturationPercent * MaxHealth, capped by current Health), 0 if nothing was applied.
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Blight")
	static float ApplySaturationDamage(UAbilitySystemComponent* SourceASC, UAbilitySystemComponent* TargetASC, AActor* InstigatorActor, float SaturationPercent = 0.15f);
};

/** Blight Rot: 10 s, 1 s period, 2.5% MaxHealth per tick, grants State.Status.BlightRot. Use ApplyBlightRot() / RemoveBlightRot(). */
UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API UAH_GE_BlightRot : public UBH_GE_StatusEffect
{
	GENERATED_BODY()

public:
	UAH_GE_BlightRot();

	/** Defaults of the three Rot tunables (UBPC_HeartFragment exposes them and passes its own values). */
	static constexpr float DefaultDuration = 10.f;
	static constexpr float TickPeriod = 1.f;
	static constexpr float DefaultTickPercent = 0.025f;
	static constexpr float DefaultStaminaRegenPenalty = 0.25f;

	/**
	 * Applies Blight Rot (and its stamina-regen penalty) to TargetASC (authority only). SourceASC may be null: the target is then its
	 * own source. Re-applying while active refreshes the duration; it never stacks.
	 * @param Duration            seconds the Rot lasts.
	 * @param TickDamagePercent   fraction of the target's MaxHealth lost per tick (0.025 = 2.5%).
	 * @param StaminaRegenPenalty fraction of passive stamina regen lost while Rot is active (0.25 = -25%).
	 * @return handle of the Rot effect (invalid if nothing was applied, e.g. the target is dead).
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Blight")
	static FActiveGameplayEffectHandle ApplyBlightRot(UAbilitySystemComponent* SourceASC, UAbilitySystemComponent* TargetASC,
		float Duration = 10.f, float TickDamagePercent = 0.025f, float StaminaRegenPenalty = 0.25f);

	/** Removes Blight Rot and its stamina penalty from ASC (authority only). @return true if a Rot was active. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Blight")
	static bool RemoveBlightRot(UAbilitySystemComponent* ASC);

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Blight")
	static bool HasBlightRot(const UAbilitySystemComponent* ASC);
};

/** Companion of UAH_GE_BlightRot: StaminaRegenRate *= SetByCaller(Data.Blight.RotStaminaRegenMult) while State.Status.BlightRot is present. */
UCLASS()
class BLACKWOODHOLLOWBETA_API UAH_GE_BlightRotStaminaPenalty : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UAH_GE_BlightRotStaminaPenalty();
};
