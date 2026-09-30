// Blackwood Hollow - Native default GameplayEffects for melee combat (implementation)

#include "AbilitySystem/Effects/AH_GE_CombatEffects.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "GameplayEffectComponents/TargetTagRequirementsGameplayEffectComponent.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"

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

	/** Configures a (constructor-created) GE component so the effect grants GrantedTag to its target while active. */
	static void ConfigureGrantedTag(UTargetTagsGameplayEffectComponent* TagsComponent, const FGameplayTag& GrantedTag)
	{
		FInheritedTagContainer TagChanges;
		TagChanges.Added.AddTag(GrantedTag);
		TagsComponent->SetAndApplyTargetTagChanges(TagChanges);
	}

	/** AddBase modifier: Attribute += RateAttribute(target, live) * Period. */
	static FGameplayModifierInfo MakeRegenModifier(const FGameplayAttribute& Attribute, const FGameplayAttribute& RateAttribute, float Period)
	{
		FAttributeBasedFloat RateBased;
		RateBased.Coefficient = FScalableFloat(Period);
		RateBased.BackingAttribute = FGameplayEffectAttributeCaptureDefinition(RateAttribute, EGameplayEffectAttributeCaptureSource::Target, /*bSnapshot*/ false);

		FGameplayModifierInfo Modifier;
		Modifier.Attribute = Attribute;
		Modifier.ModifierOp = EGameplayModOp::AddBase;
		Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(RateBased);
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

UAH_GE_PostureRegen::UAH_GE_PostureRegen()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;
	Period = FScalableFloat(RegenPeriod);
	bExecutePeriodicEffectOnApplication = false;
	PeriodicInhibitionPolicy = EGameplayEffectPeriodInhibitionRemovedPolicy::NeverReset;

	Modifiers.Add(AH_GE_CombatEffects_Private::MakeRegenModifier(
		UAH_AttributeSet::GetPostureAttribute(), UAH_AttributeSet::GetPostureRegenRateAttribute(), RegenPeriod));

	// "Ongoing" requirements switch the effect off (inhibit) while any ignore tag is present.
	UTargetTagRequirementsGameplayEffectComponent* TagRequirements =
		CreateDefaultSubobject<UTargetTagRequirementsGameplayEffectComponent>(TEXT("PostureRegenTagRequirements"));
	TagRequirements->OngoingTagRequirements.IgnoreTags.AddTag(TAG_State_Combat_Attacking);
	TagRequirements->OngoingTagRequirements.IgnoreTags.AddTag(TAG_State_Combat_Blocking);
	TagRequirements->OngoingTagRequirements.IgnoreTags.AddTag(TAG_State_Combat_PostureBroken);
	TagRequirements->OngoingTagRequirements.IgnoreTags.AddTag(TAG_State_Combat_Dead);
	TagRequirements->OngoingTagRequirements.IgnoreTags.AddTag(TAG_State_Combat_PostureRegenDelayed);
	GEComponents.Add(TagRequirements);
}

UAH_GE_StaminaRegen::UAH_GE_StaminaRegen()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;
	Period = FScalableFloat(RegenPeriod);
	bExecutePeriodicEffectOnApplication = false;
	PeriodicInhibitionPolicy = EGameplayEffectPeriodInhibitionRemovedPolicy::NeverReset;

	Modifiers.Add(AH_GE_CombatEffects_Private::MakeRegenModifier(
		UAH_AttributeSet::GetStaminaAttribute(), UAH_AttributeSet::GetStaminaRegenRateAttribute(), RegenPeriod));

	UTargetTagRequirementsGameplayEffectComponent* TagRequirements =
		CreateDefaultSubobject<UTargetTagRequirementsGameplayEffectComponent>(TEXT("StaminaRegenTagRequirements"));
	TagRequirements->OngoingTagRequirements.IgnoreTags.AddTag(TAG_State_Combat_Attacking);
	TagRequirements->OngoingTagRequirements.IgnoreTags.AddTag(TAG_State_Combat_Blocking);
	TagRequirements->OngoingTagRequirements.IgnoreTags.AddTag(TAG_State_Combat_PostureBroken);
	TagRequirements->OngoingTagRequirements.IgnoreTags.AddTag(TAG_State_Combat_Dead);
	GEComponents.Add(TagRequirements);
}

UAH_GE_Flurry::UAH_GE_Flurry()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(2.5f));

	// Additive (aggregator) mod, NOT AddBase: it stacks per application and disappears cleanly with the effect.
	FGameplayModifierInfo Modifier;
	Modifier.Attribute = UAH_AttributeSet::GetAttackSpeedAttribute();
	Modifier.ModifierOp = EGameplayModOp::Additive;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(0.05f));
	Modifiers.Add(Modifier);

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	StackingType = EGameplayEffectStackingType::AggregateBySource;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	StackLimitCount = 6;
	StackDurationRefreshPolicy = EGameplayEffectStackingDurationPolicy::RefreshOnSuccessfulApplication;
	StackExpirationPolicy = EGameplayEffectStackingExpirationPolicy::ClearEntireStack;

	UTargetTagsGameplayEffectComponent* GrantedTags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("FlurryGrantedTags"));
	AH_GE_CombatEffects_Private::ConfigureGrantedTag(GrantedTags, TAG_State_Combat_Flurry);
	GEComponents.Add(GrantedTags);
}

UAH_GE_RiposteWindow::UAH_GE_RiposteWindow()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(1.0f));

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	StackingType = EGameplayEffectStackingType::AggregateByTarget;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	StackLimitCount = 1;
	StackDurationRefreshPolicy = EGameplayEffectStackingDurationPolicy::RefreshOnSuccessfulApplication;
	StackExpirationPolicy = EGameplayEffectStackingExpirationPolicy::ClearEntireStack;

	UTargetTagsGameplayEffectComponent* GrantedTags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("RiposteGrantedTags"));
	AH_GE_CombatEffects_Private::ConfigureGrantedTag(GrantedTags, TAG_State_Combat_RiposteReady);
	GEComponents.Add(GrantedTags);
}

UAH_GE_ShieldBashCooldown::UAH_GE_ShieldBashCooldown()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(3.0f));

	UTargetTagsGameplayEffectComponent* GrantedTags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("ShieldBashCooldownTags"));
	AH_GE_CombatEffects_Private::ConfigureGrantedTag(GrantedTags, TAG_Cooldown_Combat_ShieldBash);
	GEComponents.Add(GrantedTags);
}
