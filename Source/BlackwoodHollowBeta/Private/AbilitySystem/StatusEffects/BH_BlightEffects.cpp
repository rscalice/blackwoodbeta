// Blackwood Hollow - Blight meter effects: saturation damage + Blight Rot (implementation)

#include "AbilitySystem/StatusEffects/BH_BlightEffects.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffectComponents/AssetTagsGameplayEffectComponent.h"
#include "GameplayEffectComponents/TargetTagRequirementsGameplayEffectComponent.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"

// ============================================================================
// Magnitude calculations
// ============================================================================

UAH_MMC_BlightRotTick::UAH_MMC_BlightRotTick()
{
	MaxHealthDef = FGameplayEffectAttributeCaptureDefinition(
		UAH_AttributeSet::GetMaxHealthAttribute(), EGameplayEffectAttributeCaptureSource::Target, /*bSnapshot*/ false);
	RelevantAttributesToCapture.Add(MaxHealthDef);
}

float UAH_MMC_BlightRotTick::CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const
{
	const float Fraction = Spec.GetSetByCallerMagnitude(TAG_Data_Blight_RotDamagePercent, /*WarnIfNotFound*/ false, UAH_GE_BlightRot::DefaultTickPercent);

	FAggregatorEvaluateParameters EvaluateParameters;
	EvaluateParameters.SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
	EvaluateParameters.TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();

	float MaxHealth = 0.f;
	GetCapturedAttributeMagnitude(MaxHealthDef, Spec, EvaluateParameters, MaxHealth);
	return FMath::Max(Fraction, 0.f) * FMath::Max(MaxHealth, 0.f);
}

UAH_MMC_BlightSaturation::UAH_MMC_BlightSaturation()
{
	MaxHealthDef = FGameplayEffectAttributeCaptureDefinition(
		UAH_AttributeSet::GetMaxHealthAttribute(), EGameplayEffectAttributeCaptureSource::Target, /*bSnapshot*/ false);
	RelevantAttributesToCapture.Add(MaxHealthDef);
}

float UAH_MMC_BlightSaturation::CalculateBaseMagnitude_Implementation(const FGameplayEffectSpec& Spec) const
{
	const float Fraction = Spec.GetSetByCallerMagnitude(TAG_Data_Blight_SaturationPercent, /*WarnIfNotFound*/ false, UAH_GE_BlightSaturationDamage::DefaultSaturationPercent);

	FAggregatorEvaluateParameters EvaluateParameters;
	EvaluateParameters.SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
	EvaluateParameters.TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();

	float MaxHealth = 0.f;
	GetCapturedAttributeMagnitude(MaxHealthDef, Spec, EvaluateParameters, MaxHealth);
	return FMath::Max(Fraction, 0.f) * FMath::Max(MaxHealth, 0.f);
}

// ============================================================================
// Saturation damage
// ============================================================================

UAH_GE_BlightSaturationDamage::UAH_GE_BlightSaturationDamage()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;

	FCustomCalculationBasedFloat Calculation;
	Calculation.CalculationClassMagnitude = UAH_MMC_BlightSaturation::StaticClass();

	FGameplayModifierInfo Modifier;
	Modifier.Attribute = UAH_AttributeSet::GetIncomingDamageAttribute();
	Modifier.ModifierOp = EGameplayModOp::AddBase;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(Calculation);
	Modifiers.Add(Modifier);

	// Identifies the damage as Blight for UAH_AttributeSet (no block mitigation, no automatic DamageReceived / hit reaction).
	UAssetTagsGameplayEffectComponent* AssetTags = CreateDefaultSubobject<UAssetTagsGameplayEffectComponent>(TEXT("BlightSaturationAssetTags"));
	FInheritedTagContainer AssetTagChanges;
	AssetTagChanges.Added.AddTag(TAG_Damage_Type_Blight);
	AssetTags->SetAndApplyAssetTagChanges(AssetTagChanges);
	GEComponents.Add(AssetTags);

	// Never lands on a dead target.
	UTargetTagRequirementsGameplayEffectComponent* TagRequirements =
		CreateDefaultSubobject<UTargetTagRequirementsGameplayEffectComponent>(TEXT("BlightSaturationTagRequirements"));
	TagRequirements->ApplicationTagRequirements.IgnoreTags.AddTag(TAG_State_Combat_Dead);
	GEComponents.Add(TagRequirements);
}

float UAH_GE_BlightSaturationDamage::ApplySaturationDamage(UAbilitySystemComponent* SourceASC, UAbilitySystemComponent* TargetASC,
	AActor* InstigatorActor, float SaturationPercent)
{
	if (!TargetASC || SaturationPercent <= 0.f || !TargetASC->IsOwnerActorAuthoritative())
	{
		return 0.f;
	}
	if (TargetASC->HasMatchingGameplayTag(TAG_State_Combat_Dead))
	{
		return 0.f;
	}

	const UAH_AttributeSet* TargetAttributes = TargetASC->GetSet<UAH_AttributeSet>();
	if (!TargetAttributes)
	{
		return 0.f;
	}
	const float ExpectedDamage = FMath::Min(SaturationPercent * TargetAttributes->GetMaxHealth(), TargetAttributes->GetHealth());

	UAbilitySystemComponent* const SpecOwner = SourceASC ? SourceASC : TargetASC;
	FGameplayEffectContextHandle Context = SpecOwner->MakeEffectContext();
	AActor* const Instigator = InstigatorActor ? InstigatorActor : TargetASC->GetAvatarActor();
	if (Instigator)
	{
		Context.AddInstigator(Instigator, Instigator);
	}

	const FGameplayEffectSpecHandle SpecHandle = SpecOwner->MakeOutgoingSpec(UAH_GE_BlightSaturationDamage::StaticClass(), 1.f, Context);
	if (!SpecHandle.IsValid() || !SpecHandle.Data.IsValid())
	{
		return 0.f;
	}
	SpecHandle.Data->SetSetByCallerMagnitude(TAG_Data_Blight_SaturationPercent, SaturationPercent);
	SpecOwner->ApplyGameplayEffectSpecToTarget(*SpecHandle.Data.Get(), TargetASC);
	return ExpectedDamage;
}

// ============================================================================
// Blight Rot
// ============================================================================

UAH_GE_BlightRot::UAH_GE_BlightRot()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(DefaultDuration));
	Period = FScalableFloat(TickPeriod);
	bExecutePeriodicEffectOnApplication = false;

	DisplayName = NSLOCTEXT("BlackwoodHollow", "Status_BlightRot", "Blight Rot");
	Description = NSLOCTEXT("BlackwoodHollow", "Status_BlightRot_Desc", "The Blight eats at you: you lose health every second and your stamina recovers more slowly.");
	bIsDebuff = true;

	// Periodic modifier: executes on every period, routing a % MaxHealth hit through the normal IncomingDamage -> Health -> death path.
	FCustomCalculationBasedFloat Calculation;
	Calculation.CalculationClassMagnitude = UAH_MMC_BlightRotTick::StaticClass();

	FGameplayModifierInfo Modifier;
	Modifier.Attribute = UAH_AttributeSet::GetIncomingDamageAttribute();
	Modifier.ModifierOp = EGameplayModOp::AddBase;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(Calculation);
	Modifiers.Add(Modifier);

	// One Rot per target: re-application refreshes the duration; the period timer keeps its phase.
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	StackingType = EGameplayEffectStackingType::AggregateByTarget;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	StackLimitCount = 1;
	StackDurationRefreshPolicy = EGameplayEffectStackingDurationPolicy::RefreshOnSuccessfulApplication;
	StackPeriodResetPolicy = EGameplayEffectStackingPeriodPolicy::NeverReset;
	StackExpirationPolicy = EGameplayEffectStackingExpirationPolicy::ClearEntireStack;

	// Grants State.Status.BlightRot (also recorded as StatusTag for the status-effect library / HUD).
	UTargetTagsGameplayEffectComponent* GrantedTags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("BlightRotGrantedTags"));
	ConfigureStatusTag(GrantedTags, TAG_State_Status_BlightRot);
	GEComponents.Add(GrantedTags);

	// Damage.Type.Blight: see UAH_AttributeSet (not blockable, no automatic hit reaction).
	UAssetTagsGameplayEffectComponent* AssetTags = CreateDefaultSubobject<UAssetTagsGameplayEffectComponent>(TEXT("BlightRotAssetTags"));
	FInheritedTagContainer AssetTagChanges;
	AssetTagChanges.Added.AddTag(TAG_Damage_Type_Blight);
	AssetTags->SetAndApplyAssetTagChanges(AssetTagChanges);
	GEComponents.Add(AssetTags);

	// Never applies to, and pauses on, a dead target.
	UTargetTagRequirementsGameplayEffectComponent* TagRequirements =
		CreateDefaultSubobject<UTargetTagRequirementsGameplayEffectComponent>(TEXT("BlightRotTagRequirements"));
	TagRequirements->ApplicationTagRequirements.IgnoreTags.AddTag(TAG_State_Combat_Dead);
	TagRequirements->OngoingTagRequirements.IgnoreTags.AddTag(TAG_State_Combat_Dead);
	GEComponents.Add(TagRequirements);
}

FActiveGameplayEffectHandle UAH_GE_BlightRot::ApplyBlightRot(UAbilitySystemComponent* SourceASC, UAbilitySystemComponent* TargetASC,
	float Duration, float TickDamagePercent, float StaminaRegenPenalty)
{
	if (!TargetASC || Duration <= 0.f || !TargetASC->IsOwnerActorAuthoritative())
	{
		return FActiveGameplayEffectHandle();
	}
	if (TargetASC->HasMatchingGameplayTag(TAG_State_Combat_Dead))
	{
		return FActiveGameplayEffectHandle();
	}

	UAbilitySystemComponent* const SpecOwner = SourceASC ? SourceASC : TargetASC;

	const FGameplayEffectSpecHandle RotSpec = SpecOwner->MakeOutgoingSpec(UAH_GE_BlightRot::StaticClass(), 1.f, SpecOwner->MakeEffectContext());
	if (!RotSpec.IsValid() || !RotSpec.Data.IsValid())
	{
		return FActiveGameplayEffectHandle();
	}
	RotSpec.Data->SetDuration(Duration, /*bLockDuration*/ true);
	RotSpec.Data->SetSetByCallerMagnitude(TAG_Data_Blight_RotDamagePercent, FMath::Max(TickDamagePercent, 0.f));
	const FActiveGameplayEffectHandle RotHandle = SpecOwner->ApplyGameplayEffectSpecToTarget(*RotSpec.Data.Get(), TargetASC);

	// The companion penalty needs the Rot tag to be present, so it goes second. Same duration; both refresh together.
	if (RotHandle.IsValid())
	{
		const FGameplayEffectSpecHandle PenaltySpec = SpecOwner->MakeOutgoingSpec(UAH_GE_BlightRotStaminaPenalty::StaticClass(), 1.f, SpecOwner->MakeEffectContext());
		if (PenaltySpec.IsValid() && PenaltySpec.Data.IsValid())
		{
			PenaltySpec.Data->SetDuration(Duration, /*bLockDuration*/ true);
			PenaltySpec.Data->SetSetByCallerMagnitude(TAG_Data_Blight_RotStaminaRegenMult, 1.f - FMath::Clamp(StaminaRegenPenalty, 0.f, 1.f));
			SpecOwner->ApplyGameplayEffectSpecToTarget(*PenaltySpec.Data.Get(), TargetASC);
		}
	}
	return RotHandle;
}

bool UAH_GE_BlightRot::RemoveBlightRot(UAbilitySystemComponent* ASC)
{
	if (!ASC || !ASC->IsOwnerActorAuthoritative())
	{
		return false;
	}

	// By granted tag, so Blueprint children of the Rot are caught too.
	const bool bHadRot = ASC->HasMatchingGameplayTag(TAG_State_Status_BlightRot);
	FGameplayTagContainer RotTags;
	RotTags.AddTag(TAG_State_Status_BlightRot);
	ASC->RemoveActiveEffectsWithGrantedTags(RotTags);

	FGameplayEffectQuery PenaltyQuery;
	PenaltyQuery.EffectDefinition = UAH_GE_BlightRotStaminaPenalty::StaticClass();
	ASC->RemoveActiveEffects(PenaltyQuery);
	return bHadRot;
}

bool UAH_GE_BlightRot::HasBlightRot(const UAbilitySystemComponent* ASC)
{
	return ASC && ASC->HasMatchingGameplayTag(TAG_State_Status_BlightRot);
}

UAH_GE_BlightRotStaminaPenalty::UAH_GE_BlightRotStaminaPenalty()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(UAH_GE_BlightRot::DefaultDuration));

	// MultiplyAdditive (same op as UAH_GE_ArmorWeight): final regen = base * (1 + sum(mult - 1)), so this and the armor class add up
	// and removing the effect restores the rate exactly.
	FSetByCallerFloat SetByCaller;
	SetByCaller.DataTag = TAG_Data_Blight_RotStaminaRegenMult;

	FGameplayModifierInfo Modifier;
	Modifier.Attribute = UAH_AttributeSet::GetStaminaRegenRateAttribute();
	Modifier.ModifierOp = EGameplayModOp::MultiplyAdditive;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(SetByCaller);
	Modifiers.Add(Modifier);

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	StackingType = EGameplayEffectStackingType::AggregateByTarget;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	StackLimitCount = 1;
	StackDurationRefreshPolicy = EGameplayEffectStackingDurationPolicy::RefreshOnSuccessfulApplication;
	StackExpirationPolicy = EGameplayEffectStackingExpirationPolicy::ClearEntireStack;

	// Only counts while the Rot itself is on (an early Rot removal inhibits it even before RemoveBlightRot cleans it up).
	UTargetTagRequirementsGameplayEffectComponent* TagRequirements =
		CreateDefaultSubobject<UTargetTagRequirementsGameplayEffectComponent>(TEXT("BlightRotPenaltyTagRequirements"));
	TagRequirements->OngoingTagRequirements.RequireTags.AddTag(TAG_State_Status_BlightRot);
	GEComponents.Add(TagRequirements);
}
