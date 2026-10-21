// Blackwood Hollow - the one notification toast widget (implementation, Phase 13C)

#include "UI/BH_NotificationQueueWidget.h"
#include "UI/BH_NotificationSubsystem.h"
#include "UI/BH_HUDSettings.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Engine/LocalPlayer.h"

void UBH_NotificationQueueWidget::NativeConstruct()
{
	Super::NativeConstruct();

	Alpha = 0.f;
	bFadingIn = false;
	bFadingOut = false;
	ApplyVisual(0.f);
	if (Panel_Toast)
	{
		Panel_Toast->SetVisibility(ESlateVisibility::Collapsed);
	}

	const ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	UBH_NotificationSubsystem* Queue = LocalPlayer ? LocalPlayer->GetSubsystem<UBH_NotificationSubsystem>() : nullptr;
	if (Queue && !IsDesignTime())
	{
		BoundQueue = Queue;
		Queue->RegisterPresenter();
		Queue->OnNotificationShown.AddUniqueDynamic(this, &UBH_NotificationQueueWidget::HandleShown);
		Queue->OnNotificationUpdated.AddUniqueDynamic(this, &UBH_NotificationQueueWidget::HandleUpdated);
		Queue->OnNotificationEnding.AddUniqueDynamic(this, &UBH_NotificationQueueWidget::HandleEnding);
		Queue->OnNotificationFinished.AddUniqueDynamic(this, &UBH_NotificationQueueWidget::HandleFinished);
	}
}

void UBH_NotificationQueueWidget::NativeDestruct()
{
	if (UBH_NotificationSubsystem* Queue = BoundQueue.Get())
	{
		Queue->UnregisterPresenter();
		Queue->OnNotificationShown.RemoveDynamic(this, &UBH_NotificationQueueWidget::HandleShown);
		Queue->OnNotificationUpdated.RemoveDynamic(this, &UBH_NotificationQueueWidget::HandleUpdated);
		Queue->OnNotificationEnding.RemoveDynamic(this, &UBH_NotificationQueueWidget::HandleEnding);
		Queue->OnNotificationFinished.RemoveDynamic(this, &UBH_NotificationQueueWidget::HandleFinished);
	}
	BoundQueue.Reset();
	Super::NativeDestruct();
}

void UBH_NotificationQueueWidget::ApplyTexts(const FBH_Notification& Notification)
{
	if (Txt_Title)
	{
		Txt_Title->SetText(Notification.Title);
		Txt_Title->SetVisibility(Notification.Title.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	if (Txt_Body)
	{
		Txt_Body->SetText(Notification.Body);
		Txt_Body->SetVisibility(Notification.Body.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
}

void UBH_NotificationQueueWidget::ApplyVisual(float InAlpha)
{
	if (Panel_Toast)
	{
		const UBH_HUDSettings* S = UBH_HUDSettings::Get();
		if (bFadingOut)
		{
			// Fade out while drifting a little to the right.
			Panel_Toast->SetRenderOpacity(InAlpha);
			Panel_Toast->SetRenderTranslation(FVector2D((1.f - InAlpha) * S->NotificationDriftDistance, 0.0));
		}
		else
		{
			// Slide in from off-screen right with a cubic ease-out.
			const float Eased = 1.f - FMath::Pow(1.f - FMath::Clamp(InAlpha, 0.f, 1.f), 3.f);
			Panel_Toast->SetRenderOpacity(FMath::Clamp(Eased * 2.f, 0.f, 1.f));
			Panel_Toast->SetRenderTranslation(FVector2D((1.f - Eased) * S->NotificationSlideDistance, 0.0));
		}
	}
}

void UBH_NotificationQueueWidget::HandleShown(const FBH_Notification& Notification)
{
	ApplyTexts(Notification);
	Alpha = 0.f;
	bFadingIn = true;
	bFadingOut = false;
	ApplyVisual(0.f);
	if (Panel_Toast)
	{
		Panel_Toast->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	K2_OnNotificationShown(Notification);
}

void UBH_NotificationQueueWidget::HandleUpdated(const FBH_Notification& Notification)
{
	ApplyTexts(Notification);
	K2_OnNotificationUpdated(Notification);
}

void UBH_NotificationQueueWidget::HandleEnding()
{
	bFadingIn = false;
	bFadingOut = true;
}

void UBH_NotificationQueueWidget::HandleFinished()
{
	bFadingIn = false;
	bFadingOut = false;
	Alpha = 0.f;
	ApplyVisual(0.f);
	if (Panel_Toast)
	{
		Panel_Toast->SetVisibility(ESlateVisibility::Collapsed);
	}
	K2_OnNotificationHidden();
}

void UBH_NotificationQueueWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	const UBH_HUDSettings* S = UBH_HUDSettings::Get();
	if (bFadingIn)
	{
		Alpha = FMath::Min(1.f, Alpha + InDeltaTime / FMath::Max(0.01f, S->NotificationFadeIn));
		if (Alpha >= 1.f)
		{
			bFadingIn = false;
		}
		ApplyVisual(Alpha);
	}
	else if (bFadingOut)
	{
		Alpha = FMath::Max(0.f, Alpha - InDeltaTime / FMath::Max(0.01f, S->NotificationFadeOut));
		ApplyVisual(Alpha);
	}
}
