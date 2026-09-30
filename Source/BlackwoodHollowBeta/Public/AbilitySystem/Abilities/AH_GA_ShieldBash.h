// Blackwood Hollow - Shield Bash (guard-breaker)
// Target: Unreal Engine 5.8 (C++), GAS
//
// A one-section melee ability built on UAH_GA_MeleeAttack_Base, so hit handling, damage, parry reaction and
// cues are shared. Differences: low flat damage, big posture damage that ignores the target's block, and a
// 3 s cooldown (UAH_GE_ShieldBashCooldown, tag Cooldown.Combat.ShieldBash).
//
// Author a Blueprint child with AttackMontage (one section named "Bash") carrying a UANS_MeleeHitbox
// (WeaponSlot = OffHand). Drive it with UBH_CombatFunctionLibrary::HandleMeleeAttackInput(Actor, ShieldBashClass).
// Keeps Ability.Combat.MeleeAttack so a parry still cancels it.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/AH_GA_MeleeAttack_Base.h"
#include "AH_GA_ShieldBash.generated.h"

UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API UAH_GA_ShieldBash : public UAH_GA_MeleeAttack_Base
{
	GENERATED_BODY()

public:
	UAH_GA_ShieldBash();
};
