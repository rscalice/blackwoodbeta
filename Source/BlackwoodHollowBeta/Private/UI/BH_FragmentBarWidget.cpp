// Blackwood Hollow - Heart-Fragment bar (3 slots) widget base (implementation)

#include "UI/BH_FragmentBarWidget.h"
#include "AbilitySystem/Abilities/AH_GA_FragmentBase.h"
#include "Player/BH_PlayerState.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"

void UBH_FragmentBarWidget::NativeConstruct()
{
	Super::NativeConstruct();

	SlotViews.SetNum(NumSlots);
	for (int32 Index = 0; Index < NumSlots; ++Index)
	{
		SlotViews[Index].SlotIndex = Index;
	}
	BindToPlayerState();
	RefreshAll();
}

void UBH_FragmentBarWidget::NativeDestruct()
{
	UnbindFromPlayerState();
	Super::NativeDestruct();
}

void UBH_FragmentBarWidget::BindToPlayerState()
{
	ABH_PlayerState* PS = Cast<ABH_PlayerState>(GetOwningPlayerState());
	if (PS == BoundPlayerState.Get())
	{
		return;
	}
	UnbindFromPlayerState();
	if (PS)
	{
		BoundPlayerState = PS;
		PS->OnFragmentSlotsChanged.AddUniqueDynamic(this, &UBH_FragmentBarWidget::HandleSlotsChanged);
	}
}

void UBH_FragmentBarWidget::UnbindFromPlayerState()
{
	if (ABH_PlayerState* PS = BoundPlayerState.Get())
	{
		PS->OnFragmentSlotsChanged.RemoveDynamic(this, &UBH_FragmentBarWidget::HandleSlotsChanged);
	}
	BoundPlayerState.Reset();
}

void UBH_FragmentBarWidget::HandleSlotsChanged(int32 SlotIndex)
{
	RebuildSlotViews();
	UpdateCooldowns();
	K2_OnSlotsChanged();
	K2_OnCooldownsUpdated();
}

void UBH_FragmentBarWidget::RefreshAll()
{
	RebuildSlotViews();
	UpdateCooldowns();
	K2_OnSlotsChanged();
	K2_OnCooldownsUpdated();
}

void UBH_FragmentBarWidget::RebuildSlotViews()
{
	SlotViews.SetNum(NumSlots);
	const ABH_PlayerState* PS = BoundPlayerState.Get();

	for (int32 Index = 0; Index < NumSlots; ++Index)
	{
		FBH_FragmentSlotView& View = SlotViews[Index];
		const float PreservedRemaining = View.CooldownRemaining;
		const float PreservedDuration = View.CooldownDuration;
		View = FBH_FragmentSlotView();
		View.SlotIndex = Index;
		if (!PS)
		{
			continue;
		}

		View.Category = PS->GetSlotCategory(Index);
		View.Fragment = PS->GetFragmentInSlot(Index);
		View.bEmpty = (View.Fragment == nullptr);
		if (const UAH_GA_FragmentBase* CDO = View.Fragment ? View.Fragment->GetDefaultObject<UAH_GA_FragmentBase>() : nullptr)
		{
			View.Name = CDO->FragmentName;
			View.Icon = CDO->FragmentIcon;
			View.FragmentCategory = CDO->FragmentCategory;
			View.CooldownDuration = CDO->CooldownDuration;
			View.CooldownRemaining = PreservedRemaining;
			if (PreservedDuration > 0.f)
			{
				View.CooldownDuration = PreservedDuration;
			}
		}
	}
}

const UAbilitySystemComponent* UBH_FragmentBarWidget::ResolveCooldownASC() const
{
	if (const UAbilitySystemComponent* Bound = GetHUDAbilitySystem())
	{
		return Bound;
	}
	return UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwningPlayerPawn(), true);
}

bool UBH_FragmentBarWidget::UpdateCooldowns()
{
	const UAbilitySystemComponent* ASC = ResolveCooldownASC();
	bool bChanged = false;
	bool bAnyOnCooldown = false;

	for (FBH_FragmentSlotView& View : SlotViews)
	{
		float Remaining = 0.f;
		float Duration = View.CooldownDuration;
		if (!View.bEmpty && View.Fragment)
		{
			const UAH_GA_FragmentBase* CDO = View.Fragment->GetDefaultObject<UAH_GA_FragmentBase>();
			// Cheap early-out: the cooldown effect grants the fragment's cooldown tags while it runs.
			if (ASC && CDO && ASC->HasAnyMatchingGameplayTags(CDO->CooldownTags))
			{
				UAH_GA_FragmentBase::GetCooldownRemaining(ASC, View.Fragment, Remaining, Duration);
			}
			else if (CDO)
			{
				Duration = CDO->CooldownDuration;
			}
		}

		const bool bOnCooldown = Remaining > KINDA_SMALL_NUMBER;
		const float Fraction = (bOnCooldown && Duration > KINDA_SMALL_NUMBER) ? FMath::Clamp(Remaining / Duration, 0.f, 1.f) : 0.f;
		if (bOnCooldown != View.bOnCooldown || !FMath::IsNearlyEqual(Remaining, View.CooldownRemaining, 0.001f))
		{
			bChanged = true;
		}
		View.CooldownRemaining = Remaining;
		View.CooldownDuration = Duration;
		View.CooldownFraction = Fraction;
		View.bOnCooldown = bOnCooldown;
		bAnyOnCooldown |= bOnCooldown;
	}
	bAnyOnCooldownLastPoll = bAnyOnCooldown;
	return bChanged;
}

void UBH_FragmentBarWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// The PlayerState may not exist yet (or may be replaced on respawn): keep trying until bound.
	if (!BoundPlayerState.IsValid() || BoundPlayerState.Get() != GetOwningPlayerState())
	{
		const bool bHadState = BoundPlayerState.IsValid();
		BindToPlayerState();
		if (BoundPlayerState.IsValid() || bHadState)
		{
			RefreshAll();
		}
	}

	CooldownPollTimer += InDeltaTime;
	if (CooldownPollTimer < CooldownPollInterval)
	{
		return;
	}
	CooldownPollTimer = 0.f;

	const bool bWasOnCooldown = bAnyOnCooldownLastPoll;
	const bool bChanged = UpdateCooldowns();
	if (bChanged || bWasOnCooldown || bAnyOnCooldownLastPoll)
	{
		K2_OnCooldownsUpdated();
	}
}

FBH_FragmentSlotView UBH_FragmentBarWidget::GetSlotView(int32 SlotIndex) const
{
	return SlotViews.IsValidIndex(SlotIndex) ? SlotViews[SlotIndex] : FBH_FragmentSlotView();
}

bool UBH_FragmentBarWidget::IsSlotEmpty(int32 SlotIndex) const
{
	return !SlotViews.IsValidIndex(SlotIndex) || SlotViews[SlotIndex].bEmpty;
}

UTexture2D* UBH_FragmentBarWidget::GetSlotIcon(int32 SlotIndex) const
{
	return SlotViews.IsValidIndex(SlotIndex) ? SlotViews[SlotIndex].Icon.Get() : nullptr;
}

FGameplayTag UBH_FragmentBarWidget::GetSlotCategory(int32 SlotIndex) const
{
	return SlotViews.IsValidIndex(SlotIndex) ? SlotViews[SlotIndex].Category : FGameplayTag();
}

float UBH_FragmentBarWidget::GetSlotCooldownRemaining(int32 SlotIndex) const
{
	return SlotViews.IsValidIndex(SlotIndex) ? SlotViews[SlotIndex].CooldownRemaining : 0.f;
}

float UBH_FragmentBarWidget::GetSlotCooldownFraction(int32 SlotIndex) const
{
	return SlotViews.IsValidIndex(SlotIndex) ? SlotViews[SlotIndex].CooldownFraction : 0.f;
}

bool UBH_FragmentBarWidget::IsSlotOnCooldown(int32 SlotIndex) const
{
	return SlotViews.IsValidIndex(SlotIndex) && SlotViews[SlotIndex].bOnCooldown;
}
