// Blackwood Hollow - Native default GameplayEffects for melee combat
// Target: Unreal Engine 5.8 (C++), GAS (GameplayAbilities plugin required)
//
// Instant, SetByCaller-driven effects so the melee/parry abilities work out
// of the box without authoring GE assets first. Both can be swapped for
// Blueprint GEs on the abilities (DamageEffectClass / PostureDamageEffectClass)
// as long as the replacement reads the same SetByCaller keys.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "GameplayModMagnitudeCalculation.h"
#include "AH_GE_CombatEffects.generated.h"

class UAbilitySystemComponent;

/**
 * Instant: IncomingDamage += SetByCaller(Data.Damage), RAW (no AttackPower, no Defense).
 * UAH_AttributeSet routes IncomingDamage into Health in PostGameplayEffectExecute.
 * Phase 9: melee / crab attacks now use UAH_GE_Damage_Formula; this effect stays for "true damage" callers and for legacy Blueprint
 * abilities (UAH_GA_MeleeAttack_Base / UAH_GA_CrabAttackBase send the FINAL number when DamageEffectClass is not a formula effect).
 */
UCLASS()
class BLACKWOODHOLLOWBETA_API UAH_GE_MeleeDamage : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UAH_GE_MeleeDamage();
};

/**
 * Phase 9: the one melee damage effect. Instant, runs UAH_ExecCalc_Damage:
 * IncomingDamage += ComputeDamage(Data.Damage, source AttackPower, Data.AttackPowerScale, Data.DamageMultiplier, target Defense).
 * Default DamageEffectClass of UAH_GA_MeleeAttack_Base and UAH_GA_CrabAttackBase.
 */
UCLASS()
class BLACKWOODHOLLOWBETA_API UAH_GE_Damage_Formula : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UAH_GE_Damage_Formula();
};

/**
 * Instant: Posture += SetByCaller(Data.PostureDamage).
 * Pass a NEGATIVE magnitude to deal posture damage. UAH_AttributeSet clamps
 * Posture and fires Event.Combat.PostureBreak when it reaches zero.
 */
UCLASS()
class BLACKWOODHOLLOWBETA_API UAH_GE_PostureDamage : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UAH_GE_PostureDamage();
};

/**
 * Infinite, periodic (every RegenPeriod s): Posture += PostureRegenRate * RegenPeriod.
 * The rate is read live from the target's UAH_AttributeSet::PostureRegenRate.
 * Inhibited (paused, not removed) while the target has any of:
 *   State.Combat.Attacking, Blocking, PostureBroken, Dead, PostureRegenDelayed.
 * Applied once per character (server) by UBH_CombatFunctionLibrary::ApplyPassiveRegenEffects.
 */
UCLASS()
class BLACKWOODHOLLOWBETA_API UAH_GE_PostureRegen : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UAH_GE_PostureRegen();

	/** Tick interval used for the periodic regen (also scales the per-tick amount). */
	static constexpr float RegenPeriod = 0.1f;
};

/**
 * Infinite, periodic: Stamina += StaminaRegenRate * RegenPeriod.
 * Inhibited while State.Combat.Attacking, Blocking, PostureBroken, Dead, Dodging or StaminaRegenDelayed
 * (the delay tag is set by UAH_AttributeSet for bh.Combat.StaminaRegenDelay seconds after any stamina spend).
 */
UCLASS()
class BLACKWOODHOLLOWBETA_API UAH_GE_StaminaRegen : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UAH_GE_StaminaRegen();

	static constexpr float RegenPeriod = 0.1f;
};

// ---------------------------------------------------------------------------
// Phase 7A: stamina spending
// ---------------------------------------------------------------------------

/** Returns -SetByCaller(Data.StaminaCost): lets callers pass the cost as a plain positive number. */
UCLASS()
class BLACKWOODHOLLOWBETA_API UAH_MMC_StaminaCost : public UGameplayModMagnitudeCalculation
{
	GENERATED_BODY()

public:
	virtual float CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const override;
};

/**
 * Instant: Stamina += -SetByCaller(Data.StaminaCost) (positive cost in, stamina removed).
 * UAH_AttributeSet clamps Stamina at 0 and starts the stamina regen delay on any negative delta.
 * Applied by UAH_GA_StaminaBase / UBH_CombatFunctionLibrary::ApplyStaminaCost.
 */
UCLASS()
class BLACKWOODHOLLOWBETA_API UAH_GE_StaminaCost : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UAH_GE_StaminaCost();
};

// ---------------------------------------------------------------------------
// Phase 8C: equipment stat bonuses
// ---------------------------------------------------------------------------

/**
 * Infinite: three Additive modifiers (AttackPower, Defense, MaxStamina), each driven by a SetByCaller tag
 * (Data.Equip.AttackPower / Data.Equip.Defense / Data.Equip.MaxStamina; default magnitude 0). Applied once per
 * equipped weapon by UBH_LoadoutComponent and removed when the weapon is unequipped or its loadout set is not
 * the active one. Additive (not AddBase) so removing the effect restores the base value exactly.
 */
UCLASS()
class BLACKWOODHOLLOWBETA_API UAH_GE_EquipmentStatMod : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UAH_GE_EquipmentStatMod();
};

/**
 * Phase 9: armor weight class. Infinite; StaminaRegenRate Multiplicative by SetByCaller(Data.Equip.StaminaRegenMult).
 * This base class is the Cloth effect (no tag); UAH_GE_ArmorWeight_Medium / _Heavy add State.Armor.Weight.Medium / .Heavy.
 * Applied (one at a time, the heaviest equipped class) by UBH_LoadoutComponent::RefreshArmorStatMods.
 */
UCLASS()
class BLACKWOODHOLLOWBETA_API UAH_GE_ArmorWeight : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UAH_GE_ArmorWeight();
};

/** UAH_GE_ArmorWeight + grants State.Armor.Weight.Medium. */
UCLASS()
class BLACKWOODHOLLOWBETA_API UAH_GE_ArmorWeight_Medium : public UAH_GE_ArmorWeight
{
	GENERATED_BODY()

public:
	UAH_GE_ArmorWeight_Medium();
};

/** UAH_GE_ArmorWeight + grants State.Armor.Weight.Heavy. */
UCLASS()
class BLACKWOODHOLLOWBETA_API UAH_GE_ArmorWeight_Heavy : public UAH_GE_ArmorWeight
{
	GENERATED_BODY()

public:
	UAH_GE_ArmorWeight_Heavy();
};

// ---------------------------------------------------------------------------
// Phase 6
// ---------------------------------------------------------------------------

/**
 * Dual-sword momentum. Duration 2.5 s; each application adds +0.05 to AttackSpeed
 * (Additive aggregator mod), stacking up to 6 (+30%) per source. Re-applying refreshes
 * the duration; when it expires the entire stack is cleared. Grants State.Combat.Flurry.
 * Applied to the ATTACKER by UAH_GA_MeleeAttack_Base::OnHitSelfEffectClass.
 */
UCLASS()
class BLACKWOODHOLLOWBETA_API UAH_GE_Flurry : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UAH_GE_Flurry();
};

/**
 * Perfect-parry riposte window: 1.0 s, grants State.Combat.RiposteReady to the parrier.
 * The next melee hit dealt inside the window is multiplied by UAH_GA_MeleeAttack_Base::RiposteDamageMultiplier
 * and consumes the effect. Single stack (re-parry refreshes).
 */
UCLASS()
class BLACKWOODHOLLOWBETA_API UAH_GE_RiposteWindow : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UAH_GE_RiposteWindow();
};

/**
 * Generic cooldown: HasDuration, duration = SetByCaller(Data.Cooldown). Grants NO static tags: the owning ability
 * appends its own cooldown tags to the spec's DynamicGrantedTags (see UAH_GA_FragmentBase::ApplyCooldown).
 */
UCLASS()
class BLACKWOODHOLLOWBETA_API UAH_GE_Cooldown_Base : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UAH_GE_Cooldown_Base();
};

/** Shield bash cooldown: 3 s, grants Cooldown.Combat.ShieldBash (used as UAH_GA_ShieldBash's CooldownGameplayEffectClass). */
UCLASS()
class BLACKWOODHOLLOWBETA_API UAH_GE_ShieldBashCooldown : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UAH_GE_ShieldBashCooldown();
};

// ---------------------------------------------------------------------------
// Phase 8C: Blight damage-over-time
// ---------------------------------------------------------------------------

/**
 * Magnitude of one Blight DoT tick: SetByCaller(Data.Blight.DPS) * (1 - clamp(BlightResistance, 0, 0.9)) * Period.
 * BlightResistance is the TARGET's live attribute (not snapshotted), so resistance gained mid-DoT applies to the next tick.
 * UAH_AttributeSet stores BlightResistance as a fraction (0..0.9); a value above 1 is read as a percentage (50 -> 0.5).
 */
UCLASS()
class BLACKWOODHOLLOWBETA_API UAH_MMC_BlightDoT : public UGameplayModMagnitudeCalculation
{
	GENERATED_BODY()

public:
	UAH_MMC_BlightDoT();
	virtual float CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const override;

private:
	FGameplayEffectAttributeCaptureDefinition BlightResistanceDef;
};

/**
 * Blight DoT: HasDuration (default 4 s), periodic every 1 s, each tick routes IncomingDamage (= Health damage) through the
 * normal damage path, so death / health UI / the Dead tag all work. Because the spec carries Damage.Type.Blight,
 * UAH_AttributeSet skips block mitigation and the Event.Combat.DamageReceived notification for it (no hit reaction, no flash)
 * and the effect itself deals no posture damage and plays no hit cue (no hit-stop).
 * Grants State.Status.Blighted to the target while active.
 * Stacking: AggregateByTarget, limit 1: re-applying REFRESHES the duration (period timer keeps its phase) and does not stack;
 * the DPS of the first application stays in force until the effect lapses.
 * Use ApplyBlightDoT() to apply it (sets DPS and duration).
 */
UCLASS()
class BLACKWOODHOLLOWBETA_API UAH_GE_BlightDoT : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UAH_GE_BlightDoT();

	/** Tick interval in seconds. */
	static constexpr float TickPeriod = 1.f;

	/**
	 * Applies the Blight DoT to TargetASC (authority only). SourceASC may be null (environment hazard): the target is then its own source.
	 * @param DPS                Blight damage per second BEFORE the target's BlightResistance.
	 * @param Duration           Seconds the DoT lasts (refreshed by a re-application).
	 * @param bAllowShieldAbsorb When true the target's Heart-Fragment Blight shield (UBPC_HeartFragment) soaks the expected total first
	 *                           (DPS * (1 - resistance) * Duration) and only the overflow is dealt over time.
	 * @return handle of the active effect (invalid if nothing was applied, e.g. the shield absorbed everything or the target is dead).
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Blight")
	static FActiveGameplayEffectHandle ApplyBlightDoT(UAbilitySystemComponent* SourceASC, UAbilitySystemComponent* TargetASC,
		float DPS, float Duration = 4.f, bool bAllowShieldAbsorb = true);
};
