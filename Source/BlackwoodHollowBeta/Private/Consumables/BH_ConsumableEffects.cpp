// Blackwood Hollow - consumable GameplayEffects (implementation)

#include "Consumables/BH_ConsumableEffects.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffectComponents/TargetTagRequirementsGameplayEffectComponent.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"

// ============================================================================
// Heal magnitude
// ============================================================================

UBH_MMC_ConsumableHeal::UBH_MMC_ConsumableHeal()
{
	MaxHealthDef = FGameplayEffectAttributeCaptureDefinition(
		UAH_AttributeSet::GetMaxHealthAttribute(), EGameplayEffectAttributeCaptureSource::Target, /*bSnapshot*/ false);
	RelevantAttributesToCapture.Add(MaxHealthDef);
}

float UBH_MMC_ConsumableHeal::CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const
{
	const float Fraction = Spec.GetSetByCallerMagnitude(TAG_Data_Consumable_HealFractionPerTick, /*WarnIfNotFound*/ false, 0.f);

	FAggregatorEvaluateParameters EvaluateParameters;
	EvaluateParameters.SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
	EvaluateParameters.TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();

	float MaxHealth = 0.f;
	GetCapturedAttributeMagnitude(MaxHealthDef, Spec, EvaluateParameters, MaxHealth);
	return FMath::Max(Fraction, 0.f) * FMath::Max(MaxHealth, 0.f);
}

// ============================================================================
// Heartwood Sap
// ============================================================================

UBH_GE_HeartwoodSapHeal::UBH_GE_HeartwoodSapHeal()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(3.05f)); // overridden per application (see ApplyHeal)
	Period = FScalableFloat(TickPeriod);
	bExecutePeriodicEffectOnApplication = false;

	DisplayName = NSLOCTEXT("BlackwoodHollow", "Status_HeartwoodSap", "Heartwood Sap");
	Description = NSLOCTEXT("BlackwoodHollow", "Status_HeartwoodSap_Desc", "Warm sap soaks into your wounds and mends them over a few seconds.");
	bIsDebuff = false;

	// Each period: Health += fraction * MaxHealth (clamped to MaxHealth by UAH_AttributeSet).
	FCustomCalculationBasedFloat Calculation;
	Calculation.CalculationClassMagnitude = UBH_MMC_ConsumableHeal::StaticClass();

	FGameplayModifierInfo Modifier;
	Modifier.Attribute = UAH_AttributeSet::GetHealthAttribute();
	Modifier.ModifierOp = EGameplayModOp::AddBase;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(Calculation);
	Modifiers.Add(Modifier);

	UTargetTagsGameplayEffectComponent* GrantedTags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("HeartwoodSapGrantedTags"));
	ConfigureStatusTag(GrantedTags, TAG_State_Status_HeartwoodSap);
	GEComponents.Add(GrantedTags);

	// Never heals the dead (a death during the heal pauses it; the death flow removes it with the rest of the effects).
	UTargetTagRequirementsGameplayEffectComponent* TagRequirements =
		CreateDefaultSubobject<UTargetTagRequirementsGameplayEffectComponent>(TEXT("HeartwoodSapTagRequirements"));
	TagRequirements->ApplicationTagRequirements.IgnoreTags.AddTag(TAG_State_Combat_Dead);
	TagRequirements->OngoingTagRequirements.IgnoreTags.AddTag(TAG_State_Combat_Dead);
	GEComponents.Add(TagRequirements);
}

FActiveGameplayEffectHandle UBH_GE_HeartwoodSapHeal::ApplyHeal(UAbilitySystemComponent* TargetASC, float TotalFractionOfMaxHealth, float Seconds)
{
	if (!TargetASC || TotalFractionOfMaxHealth <= 0.f || Seconds <= 0.f || !TargetASC->IsOwnerActorAuthoritative())
	{
		return FActiveGameplayEffectHandle();
	}
	if (TargetASC->HasMatchingGameplayTag(TAG_State_Combat_Dead))
	{
		return FActiveGameplayEffectHandle();
	}

	const FGameplayEffectSpecHandle Spec = TargetASC->MakeOutgoingSpec(UBH_GE_HeartwoodSapHeal::StaticClass(), 1.f, TargetASC->MakeEffectContext());
	if (!Spec.IsValid() || !Spec.Data.IsValid())
	{
		return FActiveGameplayEffectHandle();
	}

	// ceil(Seconds / period) ticks, evenly sized so the total is exactly TotalFractionOfMaxHealth.
	const int32 TickCount = FMath::Max(1, FMath::CeilToInt(Seconds / TickPeriod - KINDA_SMALL_NUMBER));
	// The last tick fires at TickCount * period; end a hair later so the final tick is never lost to timer ordering.
	Spec.Data->SetDuration(static_cast<float>(TickCount) * TickPeriod + 0.05f, /*bLockDuration*/ true);
	Spec.Data->SetSetByCallerMagnitude(TAG_Data_Consumable_HealFractionPerTick, TotalFractionOfMaxHealth / static_cast<float>(TickCount));
	return TargetASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
}

// ============================================================================
// Warden's Incense sanctuary buff
// ============================================================================

UBH_GE_WardenSanctuaryBuff::UBH_GE_WardenSanctuaryBuff()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;

	DisplayName = NSLOCTEXT("BlackwoodHollow", "Status_WardenSanctuary", "Warden's Sanctuary");
	Description = NSLOCTEXT("BlackwoodHollow", "Status_WardenSanctuary_Desc", "The Incense smoke holds the Blight back and steadies your guard: Blight fades, builds up slower, and posture recovers faster.");
	bIsDebuff = false;

	// MultiplyAdditive (same op as UAH_GE_ArmorWeight): final regen = base * (1 + sum(mult - 1)).
	FSetByCallerFloat SetByCaller;
	SetByCaller.DataTag = TAG_Data_Consumable_PostureRegenMult;

	FGameplayModifierInfo Modifier;
	Modifier.Attribute = UAH_AttributeSet::GetPostureRegenRateAttribute();
	Modifier.ModifierOp = EGameplayModOp::MultiplyAdditive;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(SetByCaller);
	Modifiers.Add(Modifier);

	UTargetTagsGameplayEffectComponent* GrantedTags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("WardenSanctuaryGrantedTags"));
	ConfigureStatusTag(GrantedTags, TAG_State_Status_WardenSanctuary);
	GEComponents.Add(GrantedTags);
}

FActiveGameplayEffectHandle UBH_GE_WardenSanctuaryBuff::Apply(UAbilitySystemComponent* TargetASC, float PostureRegenMultiplier)
{
	if (!TargetASC || !TargetASC->IsOwnerActorAuthoritative())
	{
		return FActiveGameplayEffectHandle();
	}

	const FGameplayEffectSpecHandle Spec = TargetASC->MakeOutgoingSpec(UBH_GE_WardenSanctuaryBuff::StaticClass(), 1.f, TargetASC->MakeEffectContext());
	if (!Spec.IsValid() || !Spec.Data.IsValid())
	{
		return FActiveGameplayEffectHandle();
	}
	Spec.Data->SetSetByCallerMagnitude(TAG_Data_Consumable_PostureRegenMult, FMath::Max(PostureRegenMultiplier, 0.f));
	return TargetASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
}
