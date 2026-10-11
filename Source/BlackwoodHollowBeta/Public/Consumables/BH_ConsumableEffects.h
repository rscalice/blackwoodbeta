// Blackwood Hollow - consumable GameplayEffects (Phase 11D)
// Target: Unreal Engine 5.8 (C++), GAS
//
//   UBH_GE_HeartwoodSapHeal      Status effect (State.Status.HeartwoodSap). HasDuration, periodic. Every period Health += fraction * target MaxHealth
//                                (UBH_MMC_ConsumableHeal reads SetByCaller Data.Consumable.HealFractionPerTick). UAH_AttributeSet clamps Health to
//                                MaxHealth. Not stacked: two sips are two independent heals. Use ApplyHeal().
//   UBH_GE_WardenSanctuaryBuff   Status effect (State.Status.WardenSanctuary). Infinite; PostureRegenRate is MultiplyAdditive by
//                                SetByCaller Data.Consumable.PostureRegenMult (1.5 = +50%), the same op the armor-weight classes use, so it adds up with
//                                them and removing the effect restores the rate exactly. Owned by one sanctuary per member: the sanctuary applies it
//                                when a player enters and removes it (by handle) when the player leaves or the sanctuary ends. Use Apply().
//
// Both are server-applied; the tag and the attribute changes replicate through the normal GAS paths.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/StatusEffects/BH_StatusEffect.h"
#include "GameplayEffect.h"
#include "GameplayEffectTypes.h"
#include "GameplayModMagnitudeCalculation.h"
#include "BH_ConsumableEffects.generated.h"

class UAbilitySystemComponent;

/** Magnitude of one Sap heal tick: SetByCaller(Data.Consumable.HealFractionPerTick) * the target's live MaxHealth. */
UCLASS()
class BLACKWOODHOLLOWBETA_API UBH_MMC_ConsumableHeal : public UGameplayModMagnitudeCalculation
{
	GENERATED_BODY()

public:
	UBH_MMC_ConsumableHeal();
	virtual float CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const override;

private:
	FGameplayEffectAttributeCaptureDefinition MaxHealthDef;
};

/** Heartwood Sap: heal over time as a fraction of MaxHealth. */
UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API UBH_GE_HeartwoodSapHeal : public UBH_GE_StatusEffect
{
	GENERATED_BODY()

public:
	UBH_GE_HeartwoodSapHeal();

	/** Seconds between heal ticks (0.5 s -> 6 ticks over the 3 s default). */
	static constexpr float TickPeriod = 0.5f;

	/**
	 * Applies the heal to TargetASC (authority only). The total is TotalFractionOfMaxHealth * MaxHealth, split evenly over
	 * ceil(Seconds / TickPeriod) ticks.
	 * @return handle of the heal (invalid when nothing was applied, e.g. the target is dead).
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Consumable")
	static FActiveGameplayEffectHandle ApplyHeal(UAbilitySystemComponent* TargetASC, float TotalFractionOfMaxHealth = 0.45f, float Seconds = 3.f);
};

/** Warden's Incense sanctuary buff: PostureRegenRate x multiplier while the holder stands in the sanctuary. */
UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API UBH_GE_WardenSanctuaryBuff : public UBH_GE_StatusEffect
{
	GENERATED_BODY()

public:
	UBH_GE_WardenSanctuaryBuff();

	/** Applies the buff to TargetASC (authority only). @return the handle to remove it with (invalid if nothing was applied). */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Consumable")
	static FActiveGameplayEffectHandle Apply(UAbilitySystemComponent* TargetASC, float PostureRegenMultiplier = 1.5f);
};
