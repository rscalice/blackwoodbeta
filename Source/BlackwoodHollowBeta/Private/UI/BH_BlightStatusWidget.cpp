// Blackwood Hollow - Blight status widget (implementation)

#include "UI/BH_BlightStatusWidget.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystem/StatusEffects/BH_StatusEffect.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

void UBH_BlightStatusWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (IsDesignTime())
	{
		return;
	}

	CurrentStacks = 0;
	bRotActive = false;
	LastShownRotTenths = -1.f;

	// Start hidden; the first bind shows what is needed.
	if (Txt_BlightStacks) { Txt_BlightStacks->SetVisibility(ESlateVisibility::Collapsed); }
	if (Img_BlightIcon) { Img_BlightIcon->SetVisibility(ESlateVisibility::Collapsed); }
	if (Img_RotIcon) { Img_RotIcon->SetVisibility(ESlateVisibility::Collapsed); }
	if (Txt_RotTime) { Txt_RotTime->SetVisibility(ESlateVisibility::Collapsed); }

	TryBind();
}

void UBH_BlightStatusWidget::NativeDestruct()
{
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BindTimer);
	}
	BindTimerInterval = 0.f;
	Unbind();
	Super::NativeDestruct();
}

// ----------------------------------------------------------------------------
// Binding
// ----------------------------------------------------------------------------

void UBH_BlightStatusWidget::TryBind()
{
	// The pawn may not exist yet (or may be replaced on respawn): look it up every time rather than caching it.
	const APlayerController* PC = GetOwningPlayer();
	UAbilitySystemComponent* Found = PC ? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(PC->GetPawn()) : nullptr;

	if (Found != BoundASC.Get())
	{
		if (Found)
		{
			BindTo(Found);
		}
		else
		{
			Unbind();
		}
	}

	ScheduleBindTimer(BoundASC.IsValid() ? RebindCheckInterval : BindRetryInterval);
}

void UBH_BlightStatusWidget::ScheduleBindTimer(float Interval)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	FTimerManager& Timers = World->GetTimerManager();
	if (BindTimerInterval == Interval && Timers.IsTimerActive(BindTimer))
	{
		return;
	}
	BindTimerInterval = Interval;
	Timers.SetTimer(BindTimer, this, &UBH_BlightStatusWidget::TryBind, Interval, true);
}

void UBH_BlightStatusWidget::BindTo(UAbilitySystemComponent* ASC)
{
	Unbind();
	if (!ASC)
	{
		return;
	}
	BoundASC = ASC;

	BuildupHandle = ASC->GetGameplayAttributeValueChangeDelegate(UAH_AttributeSet::GetBlightBuildupAttribute())
		.AddUObject(this, &UBH_BlightStatusWidget::HandleBuildupChanged);
	RotTagHandle = ASC->RegisterGameplayTagEvent(TAG_State_Status_BlightRot, EGameplayTagEventType::NewOrRemoved)
		.AddUObject(this, &UBH_BlightStatusWidget::HandleRotTagChanged);

	// Initialise from the current state.
	bool bFound = false;
	const float Buildup = ASC->GetGameplayAttributeValue(UAH_AttributeSet::GetBlightBuildupAttribute(), bFound);
	SetStacksFromBuildup(bFound ? Buildup : 0.f, /*bInitial*/ true);
	SetRotActive(ASC->HasMatchingGameplayTag(TAG_State_Status_BlightRot), /*bInitial*/ true);
}

void UBH_BlightStatusWidget::Unbind()
{
	if (UAbilitySystemComponent* ASC = BoundASC.Get())
	{
		if (BuildupHandle.IsValid())
		{
			ASC->GetGameplayAttributeValueChangeDelegate(UAH_AttributeSet::GetBlightBuildupAttribute()).Remove(BuildupHandle);
		}
		if (RotTagHandle.IsValid())
		{
			ASC->RegisterGameplayTagEvent(TAG_State_Status_BlightRot, EGameplayTagEventType::NewOrRemoved).Remove(RotTagHandle);
		}
	}
	BuildupHandle.Reset();
	RotTagHandle.Reset();
	BoundASC.Reset();
}

// ----------------------------------------------------------------------------
// Values
// ----------------------------------------------------------------------------

void UBH_BlightStatusWidget::HandleBuildupChanged(const FOnAttributeChangeData& Data)
{
	SetStacksFromBuildup(Data.NewValue, /*bInitial*/ false);
}

void UBH_BlightStatusWidget::HandleRotTagChanged(const FGameplayTag Tag, int32 NewCount)
{
	SetRotActive(NewCount > 0, /*bInitial*/ false);
}

void UBH_BlightStatusWidget::SetStacksFromBuildup(float Buildup, bool bInitial)
{
	const int32 NewStacks = FMath::Max(0, FMath::FloorToInt(Buildup / FMath::Max(BuildupPerStack, 1.f)));
	const bool bChanged = NewStacks != CurrentStacks;
	const int32 OldStacks = CurrentStacks;
	CurrentStacks = NewStacks;

	if (Txt_BlightStacks)
	{
		Txt_BlightStacks->SetText(FText::AsNumber(NewStacks));
		Txt_BlightStacks->SetVisibility(NewStacks > 0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (Img_BlightIcon)
	{
		Img_BlightIcon->SetVisibility(NewStacks > 0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	if (bChanged || bInitial)
	{
		OnBlightStacksChanged(NewStacks, bInitial ? 0 : OldStacks);
	}
}

void UBH_BlightStatusWidget::SetRotActive(bool bActive, bool bInitial)
{
	const bool bWas = bRotActive;
	bRotActive = bActive;
	LastShownRotTenths = -1.f;

	if (Img_RotIcon)
	{
		Img_RotIcon->SetVisibility(bActive ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (Txt_RotTime)
	{
		Txt_RotTime->SetVisibility(bActive ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	if (bActive)
	{
		RefreshRotVisuals();
	}

	if (bActive && (!bWas || bInitial))
	{
		float Duration = 0.f;
		if (const UAbilitySystemComponent* ASC = BoundASC.Get())
		{
			FBH_ActiveStatusEffect Effect;
			if (UBH_StatusEffectLibrary::GetStatusEffect(ASC, TAG_State_Status_BlightRot, Effect))
			{
				Duration = FMath::Max(Effect.Duration, 0.f);
			}
		}
		OnBlightRotStarted(Duration);
	}
	else if (!bActive && bWas)
	{
		OnBlightRotEnded();
	}
}

void UBH_BlightStatusWidget::RefreshRotVisuals()
{
	const UAbilitySystemComponent* ASC = BoundASC.Get();
	if (!ASC || !bRotActive)
	{
		return;
	}

	FBH_ActiveStatusEffect Effect;
	const bool bHave = UBH_StatusEffectLibrary::GetStatusEffect(ASC, TAG_State_Status_BlightRot, Effect);

	if (Img_RotIcon && LastShownRotTenths < 0.f)
	{
		// Icon is set once per Rot: the effect's own icon, else the fallback.
		UTexture2D* Icon = bHave ? Effect.Icon.LoadSynchronous() : nullptr;
		if (!Icon)
		{
			Icon = FallbackRotIcon;
		}
		if (Icon)
		{
			Img_RotIcon->SetBrushFromTexture(Icon);
		}
	}

	if (Txt_RotTime)
	{
		const float Remaining = bHave ? FMath::Max(Effect.Remaining, 0.f) : 0.f;
		const float Tenths = FMath::FloorToFloat(Remaining * 10.f);
		if (Tenths != LastShownRotTenths)
		{
			LastShownRotTenths = Tenths;
			FNumberFormattingOptions Opts;
			Opts.MinimumFractionalDigits = 1;
			Opts.MaximumFractionalDigits = 1;
			Txt_RotTime->SetText(FText::AsNumber(Remaining, &Opts));
		}
	}
	else
	{
		LastShownRotTenths = 0.f; // icon already applied
	}
}

void UBH_BlightStatusWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// The Rot countdown is the only per-frame work, and only while a Rot is active.
	if (bRotActive)
	{
		RefreshRotVisuals();
	}
}
