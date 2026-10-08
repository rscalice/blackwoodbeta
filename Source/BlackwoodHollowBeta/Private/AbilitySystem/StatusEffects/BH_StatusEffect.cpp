// Blackwood Hollow - status effect framework (implementation)

#include "AbilitySystem/StatusEffects/BH_StatusEffect.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"

UBH_GE_StatusEffect::UBH_GE_StatusEffect()
{
	// Status effects expire (or are removed explicitly); subclasses pick the duration policy.
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
}

void UBH_GE_StatusEffect::ConfigureStatusTag(UTargetTagsGameplayEffectComponent* TagsComponent, const FGameplayTag& Tag)
{
	StatusTag = Tag;
	if (TagsComponent)
	{
		FInheritedTagContainer TagChanges;
		TagChanges.Added.AddTag(Tag);
		TagsComponent->SetAndApplyTargetTagChanges(TagChanges);
	}
}

namespace BH_StatusEffectPrivate
{
	/** Fills Out from one active effect; false when it is not a status effect. */
	static bool BuildEntry(const UAbilitySystemComponent* ASC, const FActiveGameplayEffectHandle& Handle, float Remaining, float Duration, FBH_ActiveStatusEffect& Out)
	{
		const FActiveGameplayEffect* Active = ASC->GetActiveGameplayEffect(Handle);
		const UBH_GE_StatusEffect* Def = Active ? Cast<UBH_GE_StatusEffect>(Active->Spec.Def) : nullptr;
		if (!Def || !Def->StatusTag.IsValid())
		{
			return false;
		}

		Out.StatusTag = Def->StatusTag;
		Out.DisplayName = Def->DisplayName.IsEmpty() ? FText::FromName(Def->StatusTag.GetTagName()) : Def->DisplayName;
		Out.Icon = Def->Icon;
		Out.bIsDebuff = Def->bIsDebuff;
		Out.StackCount = FMath::Max(Active->Spec.GetStackCount(), 1);
		Out.Handle = Handle;
		Out.Remaining = Remaining;
		Out.Duration = Duration;
		return true;
	}
}

void UBH_StatusEffectLibrary::GetActiveStatusEffects(const UAbilitySystemComponent* ASC, TArray<FBH_ActiveStatusEffect>& OutEffects)
{
	OutEffects.Reset();
	if (!ASC)
	{
		return;
	}

	// An empty query matches every active effect; the cast to UBH_GE_StatusEffect is the filter (an ASC holds a handful of effects).
	// Both calls walk the same container with the same query, so the handle and time arrays line up index for index.
	const FGameplayEffectQuery Query;
	const TArray<FActiveGameplayEffectHandle> Handles = ASC->GetActiveEffects(Query);
	const TArray<TPair<float, float>> TimesAndDurations = ASC->GetActiveEffectsTimeRemainingAndDuration(Query);
	const bool bTimesValid = TimesAndDurations.Num() == Handles.Num();

	for (int32 Index = 0; Index < Handles.Num(); ++Index)
	{
		const float Remaining = bTimesValid ? TimesAndDurations[Index].Key : -1.f;
		const float Duration = bTimesValid ? TimesAndDurations[Index].Value : -1.f;

		FBH_ActiveStatusEffect Entry;
		if (BH_StatusEffectPrivate::BuildEntry(ASC, Handles[Index], Remaining, Duration, Entry))
		{
			OutEffects.Add(MoveTemp(Entry));
		}
	}
}

bool UBH_StatusEffectLibrary::GetStatusEffect(const UAbilitySystemComponent* ASC, FGameplayTag StatusTag, FBH_ActiveStatusEffect& OutEffect)
{
	OutEffect = FBH_ActiveStatusEffect();
	if (!ASC || !StatusTag.IsValid())
	{
		return false;
	}

	TArray<FBH_ActiveStatusEffect> Active;
	GetActiveStatusEffects(ASC, Active);

	// Several applications of one status can coexist (different sources): report the one that lasts longest.
	bool bFound = false;
	for (const FBH_ActiveStatusEffect& Entry : Active)
	{
		if (Entry.StatusTag != StatusTag)
		{
			continue;
		}
		const bool bLonger = !bFound || Entry.Remaining < 0.f || (OutEffect.Remaining >= 0.f && Entry.Remaining > OutEffect.Remaining);
		if (bLonger)
		{
			OutEffect = Entry;
			bFound = true;
		}
	}
	return bFound;
}

float UBH_StatusEffectLibrary::GetStatusRemainingTime(const UAbilitySystemComponent* ASC, FGameplayTag StatusTag)
{
	FBH_ActiveStatusEffect Effect;
	return GetStatusEffect(ASC, StatusTag, Effect) ? Effect.Remaining : 0.f;
}
