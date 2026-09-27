// Blackwood Hollow - Native default GameplayEffects for melee combat (implementation)

#include "AbilitySystem/Effects/AH_GE_CombatEffects.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystem/BH_GameplayTags.h"

namespace AH_GE_CombatEffects_Private
{
	static FGameplayModifierInfo MakeSetByCallerAddModifier(const FGameplayAttribute& Attribute, const FGameplayTag& DataTag)
	{
		FSetByCallerFloat SetByCaller;
		SetByCaller.DataTag = DataTag;

		FGameplayModifierInfo Modifier;
		Modifier.Attribute = Attribute;
		Modifier.ModifierOp = EGameplayModOp::AddBase;
		Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(SetByCaller);
		return Modifier;
	}
}

UAH_GE_MeleeDamage::UAH_GE_MeleeDamage()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	Modifiers.Add(AH_GE_CombatEffects_Private::MakeSetByCallerAddModifier(
		UAH_AttributeSet::GetIncomingDamageAttribute(), TAG_Data_Damage));
}

UAH_GE_PostureDamage::UAH_GE_PostureDamage()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	Modifiers.Add(AH_GE_CombatEffects_Private::MakeSetByCallerAddModifier(
		UAH_AttributeSet::GetPostureAttribute(), TAG_Data_PostureDamage));
}
