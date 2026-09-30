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
#include "AH_GE_CombatEffects.generated.h"

/**
 * Instant: IncomingDamage += SetByCaller(Data.Damage).
 * UAH_AttributeSet routes IncomingDamage into Health in PostGameplayEffectExecute.
 */
UCLASS()
class BLACKWOODHOLLOWBETA_API UAH_GE_MeleeDamage : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UAH_GE_MeleeDamage();
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
 * Inhibited while State.Combat.Attacking, Blocking, PostureBroken or Dead is present.
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

/** Shield bash cooldown: 3 s, grants Cooldown.Combat.ShieldBash (used as UAH_GA_ShieldBash's CooldownGameplayEffectClass). */
UCLASS()
class BLACKWOODHOLLOWBETA_API UAH_GE_ShieldBashCooldown : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UAH_GE_ShieldBashCooldown();
};
