// Blackwood Hollow - quest tracker slot under the compass (implementation, Phase 13C)

#include "UI/BH_QuestTrackerWidget.h"
#include "Beats/BH_BeatTrackerComponent.h"
#include "Beats/BH_BeatTypes.h"
#include "Components/TextBlock.h"

void UBH_QuestTrackerWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (Txt_Header)
	{
		Txt_Header->SetText(HeaderText);
	}
	bHadText = false;
	ObjectiveText = FText::GetEmpty();
	Refresh();
	if (!IsDesignTime())
	{
		TryBind();
	}
}

void UBH_QuestTrackerWidget::NativeDestruct()
{
	Unbind();
	Super::NativeDestruct();
}

void UBH_QuestTrackerWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// The game state (and its beat tracker) may not exist yet on a client: keep trying until bound.
	if (!BoundTracker.IsValid())
	{
		RetryTimer -= InDeltaTime;
		if (RetryTimer <= 0.f)
		{
			RetryTimer = 0.25f;
			TryBind();
		}
	}
}

void UBH_QuestTrackerWidget::TryBind()
{
	UBH_BeatTrackerComponent* Tracker = UBH_BeatTrackerComponent::Get(this);
	if (!Tracker || Tracker == BoundTracker.Get())
	{
		return;
	}
	Unbind();
	BoundTracker = Tracker;
	Tracker->OnObjectiveChanged.AddUniqueDynamic(this, &UBH_QuestTrackerWidget::HandleObjectiveChanged);
	Tracker->OnBeatChanged.AddUniqueDynamic(this, &UBH_QuestTrackerWidget::HandleBeatChanged);
	Refresh();
}

void UBH_QuestTrackerWidget::Unbind()
{
	if (UBH_BeatTrackerComponent* Tracker = BoundTracker.Get())
	{
		Tracker->OnObjectiveChanged.RemoveDynamic(this, &UBH_QuestTrackerWidget::HandleObjectiveChanged);
		Tracker->OnBeatChanged.RemoveDynamic(this, &UBH_QuestTrackerWidget::HandleBeatChanged);
	}
	BoundTracker.Reset();
}

void UBH_QuestTrackerWidget::HandleObjectiveChanged()
{
	Refresh();
}

void UBH_QuestTrackerWidget::HandleBeatChanged(FGameplayTag NewBeat, FGameplayTag OldBeat)
{
	Refresh();
}

void UBH_QuestTrackerWidget::Refresh()
{
	FText NewText;
	if (UBH_BeatTrackerComponent* Tracker = BoundTracker.Get())
	{
		FBH_ObjectiveInfo Info;
		if (Tracker->GetCurrentObjective(Info))
		{
			NewText = Info.ObjectiveText;
		}
	}

	const bool bChanged = !NewText.EqualTo(ObjectiveText);
	ObjectiveText = NewText;
	if (Txt_Objective)
	{
		Txt_Objective->SetText(ObjectiveText);
	}
	if (Panel_Root)
	{
		Panel_Root->SetVisibility(ObjectiveText.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	if (bChanged && !ObjectiveText.IsEmpty())
	{
		K2_OnObjectiveChanged(ObjectiveText, !bHadText);
		bHadText = true;
	}
}
