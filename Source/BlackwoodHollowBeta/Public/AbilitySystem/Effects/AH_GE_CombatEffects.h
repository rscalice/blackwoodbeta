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
