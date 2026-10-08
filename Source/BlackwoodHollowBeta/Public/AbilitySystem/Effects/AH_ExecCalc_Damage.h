// Blackwood Hollow - Phase 9 damage execution calculation
// Target: Unreal Engine 5.8 (C++), GAS (GameplayAbilities plugin required)
//
// The one place a melee / crab hit becomes Health damage. Used by UAH_GE_Damage_Formula.
//   captures : source AttackPower (snapshot), target Defense (live)
//   SetByCaller keys on the spec:
//     Data.Damage           base / weapon damage (REQUIRED; without it nothing is dealt and a warning is logged)
//     Data.DamageMultiplier total multiplier (default 1)
//     Data.AttackPowerScale share of AttackPower added to the base (default 1; shield bash passes 0)
//   output   : Additive on UAH_AttributeSet::IncomingDamage = UBH_CombatFunctionLibrary::ComputeDamage(...)
// UAH_AttributeSet then applies parry / dodge i-frames / block mitigation to IncomingDamage as before.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectExecutionCalculation.h"
#include "GameplayEffectTypes.h"
#include "AH_ExecCalc_Damage.generated.h"

UCLASS()
class BLACKWOODHOLLOWBETA_API UAH_ExecCalc_Damage : public UGameplayEffectExecutionCalculation
{
	GENERATED_BODY()

public:
	UAH_ExecCalc_Damage();

	virtual void Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams,
		FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const override;

private:
	FGameplayEffectAttributeCaptureDefinition AttackPowerDef;
	FGameplayEffectAttributeCaptureDefinition DefenseDef;
};
