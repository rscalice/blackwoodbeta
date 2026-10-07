// Blackwood Hollow - Native default GameplayEffects for melee combat (implementation)

#include "AbilitySystem/Effects/AH_GE_CombatEffects.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "Components/BPC_HeartFragment.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffectComponents/AssetTagsGameplayEffectComponent.h"
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

UAH_GE_EquipmentStatMod::UAH_GE_EquipmentStatMod()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;

	auto AddAdditive = [this](const FGameplayAttribute& Attribute, const FGameplayTag& DataTag)
	{
		FSetByCallerFloat SetByCaller;
		SetByCaller.DataTag = DataTag;

		FGameplayModifierInfo Modifier;
		Modifier.Attribute = Attribute;
		Modifier.ModifierOp = EGameplayModOp::Additive;
		Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(SetByCaller);
		Modifiers.Add(Modifier);
	};
	AddAdditive(UAH_AttributeSet::GetAttackPowerAttribute(), TAG_Data_Equip_AttackPower);
	AddAdditive(UAH_AttributeSet::GetDefenseAttribute(), TAG_Data_Equip_Defense);
	AddAdditive(UAH_AttributeSet::GetMaxStaminaAttribute(), TAG_Data_Equip_MaxStamina);
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

float UAH_MMC_StaminaCost::CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const
{
	return -FMath::Max(Spec.GetSetByCallerMagnitude(TAG_Data_StaminaCost, /*WarnIfNotFound*/ false, 0.f), 0.f);
}

UAH_GE_StaminaCost::UAH_GE_StaminaCost()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;

	FCustomCalculationBasedFloat Calculation;
	Calculation.CalculationClassMagnitude = UAH_MMC_StaminaCost::StaticClass();

	FGameplayModifierInfo Modifier;
	Modifier.Attribute = UAH_AttributeSet::GetStaminaAttribute();
	Modifier.ModifierOp = EGameplayModOp::AddBase;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(Calculation);
	Modifiers.Add(Modifier);
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
	TagRequirements->OngoingTagRequirements.IgnoreTags.AddTag(TAG_State_Combat_Dodging);
	TagRequirements->OngoingTagRequirements.IgnoreTags.AddTag(TAG_State_Combat_StaminaRegenDelayed);
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

UAH_GE_Cooldown_Base::UAH_GE_Cooldown_Base()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;

	FSetByCallerFloat SetByCaller;
	SetByCaller.DataTag = TAG_Data_Cooldown;
	DurationMagnitude = FGameplayEffectModifierMagnitude(SetByCaller);
}

UAH_GE_ShieldBashCooldown::UAH_GE_ShieldBashCooldown()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(3.0f));

	UTargetTagsGameplayEffectComponent* GrantedTags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("ShieldBashCooldownTags"));
	AH_GE_CombatEffects_Private::ConfigureGrantedTag(GrantedTags, TAG_Cooldown_Combat_ShieldBash);
	GEComponents.Add(GrantedTags);
}

// ---------------------------------------------------------------------------
// Phase 8C: Blight damage-over-time
// ---------------------------------------------------------------------------

UAH_MMC_BlightDoT::UAH_MMC_BlightDoT()
{
	BlightResistanceDef = FGameplayEffectAttributeCaptureDefinition(
		UAH_AttributeSet::GetBlightResistanceAttribute(), EGameplayEffectAttributeCaptureSource::Target, /*bSnapshot*/ false);
	RelevantAttributesToCapture.Add(BlightResistanceDef);
}

float UAH_MMC_BlightDoT::CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const
{
	const float DPS = FMath::Max(Spec.GetSetByCallerMagnitude(TAG_Data_Blight_DPS, /*WarnIfNotFound*/ false, 0.f), 0.f);

	FAggregatorEvaluateParameters EvaluateParameters;
	EvaluateParameters.SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
	EvaluateParameters.TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();

	float Resistance = 0.f;
	GetCapturedAttributeMagnitude(BlightResistanceDef, Spec, EvaluateParameters, Resistance);
	const float ResistanceFraction = FMath::Clamp(Resistance > 1.f ? Resistance / 100.f : Resistance, 0.f, 0.9f);

	const float Period = Spec.GetPeriod() > 0.f ? Spec.GetPeriod() : UAH_GE_BlightDoT::TickPeriod;
	return DPS * (1.f - ResistanceFraction) * Period;
}

UAH_GE_BlightDoT::UAH_GE_BlightDoT()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(4.f));
	Period = FScalableFloat(TickPeriod);
	bExecutePeriodicEffectOnApplication = false;

	FCustomCalculationBasedFloat Calculation;
	Calculation.CalculationClassMagnitude = UAH_MMC_BlightDoT::StaticClass();

	FGameplayModifierInfo Modifier;
	Modifier.Attribute = UAH_AttributeSet::GetIncomingDamageAttribute();
	Modifier.ModifierOp = EGameplayModOp::AddBase;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(Calculation);
	Modifiers.Add(Modifier);

	// One DoT per target: re-application refreshes the duration; the period timer keeps running so continuous re-application still ticks.
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	StackingType = EGameplayEffectStackingType::AggregateByTarget;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	StackLimitCount = 1;
	StackDurationRefreshPolicy = EGameplayEffectStackingDurationPolicy::RefreshOnSuccessfulApplication;
	StackPeriodResetPolicy = EGameplayEffectStackingPeriodPolicy::NeverReset;
	StackExpirationPolicy = EGameplayEffectStackingExpirationPolicy::ClearEntireStack;

	UTargetTagsGameplayEffectComponent* GrantedTags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("BlightDoTGrantedTags"));
	AH_GE_CombatEffects_Private::ConfigureGrantedTag(GrantedTags, TAG_State_Status_Blighted);
	GEComponents.Add(GrantedTags);

	// Asset tag: identifies the damage as Blight for UAH_AttributeSet (also matched by class, so Blueprint children work either way).
	UAssetTagsGameplayEffectComponent* AssetTags = CreateDefaultSubobject<UAssetTagsGameplayEffectComponent>(TEXT("BlightDoTAssetTags"));
	FInheritedTagContainer AssetTagChanges;
	AssetTagChanges.Added.AddTag(TAG_Damage_Type_Blight);
	AssetTags->SetAndApplyAssetTagChanges(AssetTagChanges);
	GEComponents.Add(AssetTags);

	// Never applies to, and pauses on, a dead target.
	UTargetTagRequirementsGameplayEffectComponent* TagRequirements =
		CreateDefaultSubobject<UTargetTagRequirementsGameplayEffectComponent>(TEXT("BlightDoTTagRequirements"));
	TagRequirements->ApplicationTagRequirements.IgnoreTags.AddTag(TAG_State_Combat_Dead);
	TagRequirements->OngoingTagRequirements.IgnoreTags.AddTag(TAG_State_Combat_Dead);
	GEComponents.Add(TagRequirements);
}

FActiveGameplayEffectHandle UAH_GE_BlightDoT::ApplyBlightDoT(UAbilitySystemComponent* SourceASC, UAbilitySystemComponent* TargetASC,
	float DPS, float Duration, bool bAllowShieldAbsorb)
{
	if (!TargetASC || DPS <= 0.f || Duration <= 0.f || !TargetASC->IsOwnerActorAuthoritative())
	{
		return FActiveGameplayEffectHandle();
	}
	if (TargetASC->HasMatchingGameplayTag(TAG_State_Combat_Dead))
	{
		return FActiveGameplayEffectHandle();
	}

	UAbilitySystemComponent* const SpecOwner = SourceASC ? SourceASC : TargetASC;

	float EffectiveDPS = DPS;
	if (bAllowShieldAbsorb)
	{
		// The Heart-Fragment Blight shield soaks the expected total up front; only the overflow is dealt over time.
		const AActor* TargetAvatar = TargetASC->GetAvatarActor();
		UBPC_HeartFragment* HeartFragment = TargetAvatar ? TargetAvatar->FindComponentByClass<UBPC_HeartFragment>() : nullptr;
		const UAH_AttributeSet* TargetAttributes = TargetASC->GetSet<UAH_AttributeSet>();
		if (HeartFragment && TargetAttributes)
		{
			const float Resistance = TargetAttributes->GetBlightResistance();
			const float ResistanceFraction = FMath::Clamp(Resistance > 1.f ? Resistance / 100.f : Resistance, 0.f, 0.9f);
			const float ExpectedTotal = DPS * (1.f - ResistanceFraction) * Duration;
			if (ExpectedTotal > 0.f)
			{
				const float Overflow = HeartFragment->AbsorbBlightDamage(ExpectedTotal);
				EffectiveDPS = DPS * FMath::Clamp(Overflow / ExpectedTotal, 0.f, 1.f);
			}
		}
	}
	if (EffectiveDPS <= KINDA_SMALL_NUMBER)
	{
		return FActiveGameplayEffectHandle();
	}

	const FGameplayEffectSpecHandle SpecHandle = SpecOwner->MakeOutgoingSpec(UAH_GE_BlightDoT::StaticClass(), 1.f, SpecOwner->MakeEffectContext());
	if (!SpecHandle.IsValid() || !SpecHandle.Data.IsValid())
	{
		return FActiveGameplayEffectHandle();
	}

	SpecHandle.Data->SetDuration(Duration, /*bLockDuration*/ true);
	SpecHandle.Data->SetSetByCallerMagnitude(TAG_Data_Blight_DPS, EffectiveDPS);
	return SpecOwner->ApplyGameplayEffectSpecToTarget(*SpecHandle.Data.Get(), TargetASC);
}
