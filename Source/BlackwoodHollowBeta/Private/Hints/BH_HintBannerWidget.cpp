// Blackwood Hollow - first-time hint banner (Phase 13E, implementation)

#include "Hints/BH_HintBannerWidget.h"
#include "Hints/BH_HintData.h"
#include "Hints/BH_HintSubsystem.h"
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
	Phase = EPhase::Idle;
	Super::NativeDestruct();
}

bool UBH_HintBannerWidget::ShowHint(const UBH_HintData& Hint, float HoldSeconds)
{
	if (IsShowing())
	{
		return false;
	}
	TArray<UInputAction*> Actions;
	for (const TObjectPtr<UInputAction>& Action : Hint.GlyphActions)
	{
		if (Action)
		{
			Actions.Add(Action);
		}
	}
	OnHintPresented(Hint.Title, Hint.Text, Actions);
	HoldTime = FMath::Max(0.f, HoldSeconds);
	PhaseTime = 0.f;
	Phase = EPhase::FadeIn;
	SetRenderOpacity(0.f);
	SetRenderTranslation(FVector2D(0.0, UBH_HintSettings::Get()->BannerRisePixels));
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
	case EPhase::FadeIn:
	{
		const float Duration = FMath::Max(Settings->BannerFadeInSeconds, KINDA_SMALL_NUMBER);
		const float Alpha = FMath::Clamp(PhaseTime / Duration, 0.f, 1.f);
		const float Eased = FMath::InterpEaseOut(0.f, 1.f, Alpha, 2.f);
		SetRenderOpacity(Eased);
		SetRenderTranslation(FVector2D(0.0, Settings->BannerRisePixels * (1.f - Eased)));
		if (Alpha >= 1.f)
		{
			Phase = EPhase::Hold;
			PhaseTime = 0.f;
		}
		break;
	}
	case EPhase::Hold:
		if (PhaseTime >= HoldTime)
		{
			Phase = EPhase::FadeOut;
			PhaseTime = 0.f;
		}
		break;
	case EPhase::FadeOut:
	{
		const float Duration = FMath::Max(Settings->BannerFadeOutSeconds, KINDA_SMALL_NUMBER);
		const float Alpha = FMath::Clamp(PhaseTime / Duration, 0.f, 1.f);
		SetRenderOpacity(1.f - Alpha);
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
	Phase = EPhase::Idle;
	PhaseTime = 0.f;
	SetRenderOpacity(0.f);
	SetRenderTranslation(FVector2D::ZeroVector);
	OnFinished.Broadcast();
}
