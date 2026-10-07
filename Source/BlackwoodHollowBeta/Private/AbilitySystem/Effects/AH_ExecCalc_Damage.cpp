// Blackwood Hollow - Phase 9 damage execution calculation (implementation)

#include "AbilitySystem/Effects/AH_ExecCalc_Damage.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystem/BH_CombatFunctionLibrary.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "GameplayEffect.h"

UAH_ExecCalc_Damage::UAH_ExecCalc_Damage()
{
	// Attacker's AttackPower at the moment the spec was made (the swing that produced the hit); defender's Defense when it lands.
	AttackPowerDef = FGameplayEffectAttributeCaptureDefinition(
		UAH_AttributeSet::GetAttackPowerAttribute(), EGameplayEffectAttributeCaptureSource::Source, /*bSnapshot*/ true);
	DefenseDef = FGameplayEffectAttributeCaptureDefinition(
		UAH_AttributeSet::GetDefenseAttribute(), EGameplayEffectAttributeCaptureSource::Target, /*bSnapshot*/ false);

	RelevantAttributesToCapture.Add(AttackPowerDef);
	RelevantAttributesToCapture.Add(DefenseDef);
}

void UAH_ExecCalc_Damage::Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams,
	FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const
{
	const FGameplayEffectSpec& Spec = ExecutionParams.GetOwningSpec();

	if (!Spec.SetByCallerTagMagnitudes.Contains(TAG_Data_Damage.GetTag()))
	{
		UE_LOG(LogBHCombat, Warning, TEXT("UAH_ExecCalc_Damage: spec of '%s' has no Data.Damage SetByCaller; no damage dealt."), *GetNameSafe(Spec.Def));
		return;
	}

	FAggregatorEvaluateParameters EvaluateParameters;
	EvaluateParameters.SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
	EvaluateParameters.TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();

	float AttackPower = 0.f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(AttackPowerDef, EvaluateParameters, AttackPower);
	float Defense = 0.f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(DefenseDef, EvaluateParameters, Defense);

	const float BaseDamage = Spec.GetSetByCallerMagnitude(TAG_Data_Damage, /*WarnIfNotFound*/ true, 0.f);
	const float Multiplier = Spec.GetSetByCallerMagnitude(TAG_Data_DamageMultiplier, /*WarnIfNotFound*/ false, 1.f);
	const float AttackPowerScale = Spec.GetSetByCallerMagnitude(TAG_Data_AttackPowerScale, /*WarnIfNotFound*/ false, 1.f);

	const float Damage = UBH_CombatFunctionLibrary::ComputeDamage(BaseDamage, AttackPower, AttackPowerScale, Multiplier, Defense);

	OutExecutionOutput.AddOutputModifier(FGameplayModifierEvaluatedData(
		UAH_AttributeSet::GetIncomingDamageAttribute(), EGameplayModOp::Additive, Damage));
}
