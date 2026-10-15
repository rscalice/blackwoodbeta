// Blackwood Hollow - first-time hint banner (Phase 13E, implementation)

#include "Hints/BH_HintBannerWidget.h"
#include "Hints/BH_HintData.h"
#include "Hints/BH_HintSubsystem.h"
#include "UI/BH_QuestTrackerWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Engine/LocalPlayer.h"
#include "InputAction.h"

void UBH_HintBannerWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetRenderOpacity(0.f);
	SetVisibility(ESlateVisibility::HitTestInvisible);
	if (const ULocalPlayer* LocalPlayer = GetOwningLocalPlayer())
	{
		if (UBH_HintSubsystem* Hints = LocalPlayer->GetSubsystem<UBH_HintSubsystem>())
		{
			Hints->RegisterBanner(this);
		}
	}
}

void UBH_HintBannerWidget::NativeDestruct()
{
	if (const ULocalPlayer* LocalPlayer = GetOwningLocalPlayer())
	{
		if (UBH_HintSubsystem* Hints = LocalPlayer->GetSubsystem<UBH_HintSubsystem>())
		{
			Hints->UnregisterBanner(this);
		}
	}
	if (Phase != EPhase::Idle)
	{
		SetTrackerOpacity(1.f); // never leave the objective hidden
	}
	Phase = EPhase::Idle;
	Super::NativeDestruct();
}

UWidget* UBH_HintBannerWidget::FindTracker()
{
	if (Tracker.IsValid())
	{
		return Tracker.Get();
	}
	TArray<UUserWidget*> Found;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Found, UBH_QuestTrackerWidget::StaticClass(), false);
	for (UUserWidget* Candidate : Found)
	{
		if (Candidate && Candidate->GetOwningLocalPlayer() == GetOwningLocalPlayer())
		{
			Tracker = Candidate;
			break;
		}
	}
	return Tracker.Get();
}

void UBH_HintBannerWidget::SetTrackerOpacity(float Alpha)
{
	if (UWidget* Target = Tracker.Get())
	{
		Target->SetRenderOpacity(FMath::Clamp(Alpha, 0.f, 1.f));
	}
}

void UBH_HintBannerWidget::EnterPhase(EPhase NewPhase)
{
	Phase = NewPhase;
	PhaseTime = 0.f;
}

bool UBH_HintBannerWidget::ShowHint(const UBH_HintData& Hint, float HoldSeconds)
{
	if (IsShowing())
	{
		return false;
	}
	// One glyph per hint: the first action's primary key on the active device.
	TArray<UInputAction*> Actions;
	for (const TObjectPtr<UInputAction>& Action : Hint.GlyphActions)
	{
		if (Action)
		{
			Actions.Add(Action);
			break;
		}
	}
	OnHintPresented(Hint.Title, Hint.Text, Actions);
	HoldTime = FMath::Max(0.f, HoldSeconds);
	SetRenderOpacity(0.f);
	SetRenderTranslation(FVector2D(0.0, UBH_HintSettings::Get()->BannerRisePixels));

	// Take the objective slot: the objective goes first, then the hint. With no tracker (or a zero swap time) the hint just fades in.
	const bool bSwap = FindTracker() != nullptr && UBH_HintSettings::Get()->TrackerSwapSeconds > KINDA_SMALL_NUMBER;
	EnterPhase(bSwap ? EPhase::TrackerOut : EPhase::FadeIn);
	return true;
}

void UBH_HintBannerWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (Phase == EPhase::Idle)
	{
		return;
	}
	const UBH_HintSettings* Settings = UBH_HintSettings::Get();
	PhaseTime += InDeltaTime;

	switch (Phase)
	{
	case EPhase::TrackerOut:
	{
		const float Duration = FMath::Max(Settings->TrackerSwapSeconds, KINDA_SMALL_NUMBER);
		const float Alpha = FMath::Clamp(PhaseTime / Duration, 0.f, 1.f);
		SetTrackerOpacity(1.f - Alpha);
		if (Alpha >= 1.f)
		{
			EnterPhase(EPhase::FadeIn);
		}
		break;
	}
	case EPhase::FadeIn:
	{
		const float Duration = FMath::Max(Settings->BannerFadeInSeconds, KINDA_SMALL_NUMBER);
		const float Alpha = FMath::Clamp(PhaseTime / Duration, 0.f, 1.f);
		const float Eased = FMath::InterpEaseOut(0.f, 1.f, Alpha, 2.f);
		SetRenderOpacity(Eased);
		SetRenderTranslation(FVector2D(0.0, Settings->BannerRisePixels * (1.f - Eased)));
		if (Alpha >= 1.f)
		{
			EnterPhase(EPhase::Hold);
		}
		break;
	}
	case EPhase::Hold:
		if (PhaseTime >= HoldTime)
		{
			EnterPhase(EPhase::FadeOut);
		}
		break;
	case EPhase::FadeOut:
	{
		const float Duration = FMath::Max(Settings->BannerFadeOutSeconds, KINDA_SMALL_NUMBER);
		const float Alpha = FMath::Clamp(PhaseTime / Duration, 0.f, 1.f);
		SetRenderOpacity(1.f - Alpha);
		if (Alpha >= 1.f)
		{
			SetRenderOpacity(0.f);
			if (Tracker.IsValid() && Settings->TrackerSwapSeconds > KINDA_SMALL_NUMBER)
			{
				EnterPhase(EPhase::TrackerIn);
			}
			else
			{
				Finish();
			}
		}
		break;
	}
	case EPhase::TrackerIn:
	{
		const float Duration = FMath::Max(Settings->TrackerSwapSeconds, KINDA_SMALL_NUMBER);
		const float Alpha = FMath::Clamp(PhaseTime / Duration, 0.f, 1.f);
		SetTrackerOpacity(Alpha);
		if (Alpha >= 1.f)
		{
			Finish();
		}
		break;
	}
	default:
		break;
	}
}

void UBH_HintBannerWidget::Finish()
{
	SetTrackerOpacity(1.f);
	Phase = EPhase::Idle;
	PhaseTime = 0.f;
	SetRenderOpacity(0.f);
	SetRenderTranslation(FVector2D::ZeroVector);
	OnFinished.Broadcast();
}
