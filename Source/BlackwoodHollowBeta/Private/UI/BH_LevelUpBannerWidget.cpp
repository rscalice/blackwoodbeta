// Blackwood Hollow - level-up banner widget (implementation)

#include "UI/BH_LevelUpBannerWidget.h"
#include "Animation/WidgetAnimation.h"
#include "Components/TextBlock.h"

namespace
{
	/** Stat entries are joined with this (middle dot). */
	const TCHAR* const StatSeparator = TEXT(" · ");

	/** Exponent of the ease-out used for the fade in / slide. */
	constexpr float EaseExponent = 2.f;
}

void UBH_LevelUpBannerWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// Collapse when Anim_Show finishes. Bound once per instance (the animation is bound to the widget before this runs).
	if (Anim_Show && !bAnimFinishedBound)
	{
		FWidgetAnimationDynamicEvent FinishedEvent;
		FinishedEvent.BindDynamic(this, &UBH_LevelUpBannerWidget::HandleShowAnimFinished);
		BindToAnimationFinished(Anim_Show, FinishedEvent);
		bAnimFinishedBound = true;
	}
}

void UBH_LevelUpBannerWidget::NativeConstruct()
{
	Super::NativeConstruct();

	bNativeActive = false;
	ElapsedNative = 0.f;
	SetVisibility(ESlateVisibility::Collapsed);
}

FText UBH_LevelUpBannerWidget::BuildStatsText(const FBH_LevelUpInfo& Info)
{
	TArray<FString> Parts;
	const int32 Health = FMath::RoundToInt(Info.HealthGain);
	const int32 Posture = FMath::RoundToInt(Info.PostureGain);
	const int32 Stamina = FMath::RoundToInt(Info.StaminaGain);
	if (Health > 0)
	{
		Parts.Add(FText::Format(NSLOCTEXT("BHLevelUp", "HealthGain", "+{0} Health"), FText::AsNumber(Health)).ToString());
	}
	if (Posture > 0)
	{
		Parts.Add(FText::Format(NSLOCTEXT("BHLevelUp", "PostureGain", "+{0} Posture"), FText::AsNumber(Posture)).ToString());
	}
	if (Stamina > 0)
	{
		Parts.Add(FText::Format(NSLOCTEXT("BHLevelUp", "StaminaGain", "+{0} Stamina"), FText::AsNumber(Stamina)).ToString());
	}
	return FText::FromString(FString::Join(Parts, StatSeparator));
}

void UBH_LevelUpBannerWidget::ShowLevelUp(const FBH_LevelUpInfo& Info)
{
	if (LevelText)
	{
		LevelText->SetText(FText::Format(NSLOCTEXT("BHLevelUp", "LevelFmt", "LEVEL {0}"), FText::AsNumber(Info.NewLevel)));
	}
	if (StatsText)
	{
		const FText Stats = BuildStatsText(Info);
		StatsText->SetText(Stats);
		StatsText->SetVisibility(Stats.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	}

	// Never blocks the mouse; visible for the whole display.
	SetVisibility(ESlateVisibility::HitTestInvisible);

	if (Anim_Show)
	{
		// The animation owns opacity / position. Restart it from the top; HandleShowAnimFinished ignores the stop of the previous run.
		bNativeActive = false;
		SetRenderOpacity(1.f);
		SetRenderTranslation(FVector2D::ZeroVector);
		StopAnimation(Anim_Show);
		PlayAnimation(Anim_Show);
	}
	else
	{
		bNativeActive = true;
		ElapsedNative = 0.f;
		ApplyNativeFrame();
	}

	OnShown(Info);
}

void UBH_LevelUpBannerWidget::HideBanner()
{
	bNativeActive = false;
	ElapsedNative = 0.f;
	if (Anim_Show)
	{
		StopAnimation(Anim_Show);
	}
	SetRenderOpacity(1.f);
	SetRenderTranslation(FVector2D::ZeroVector);
	SetVisibility(ESlateVisibility::Collapsed);
}

void UBH_LevelUpBannerWidget::HandleShowAnimFinished()
{
	// A restart stops the running animation first, which can report "finished" for the old run: only a real end collapses.
	if (Anim_Show && IsAnimationPlaying(Anim_Show))
	{
		return;
	}
	SetVisibility(ESlateVisibility::Collapsed);
}

void UBH_LevelUpBannerWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!bNativeActive)
	{
		return;
	}

	ElapsedNative += InDeltaTime;
	if (ElapsedNative >= FadeIn + HoldTime + FadeOut)
	{
		HideBanner();
		return;
	}
	ApplyNativeFrame();
}

void UBH_LevelUpBannerWidget::ApplyNativeFrame()
{
	float Opacity = 1.f;
	float OffsetY = 0.f;

	if (ElapsedNative < FadeIn)
	{
		// In: fade up while sliding down from SlideDistance pixels above the resting position.
		const float Alpha = FMath::InterpEaseOut(0.f, 1.f, FMath::Clamp(ElapsedNative / FMath::Max(FadeIn, KINDA_SMALL_NUMBER), 0.f, 1.f), EaseExponent);
		Opacity = Alpha;
		OffsetY = -SlideDistance * (1.f - Alpha);
	}
	else if (ElapsedNative >= FadeIn + HoldTime)
	{
		// Out: fade away in place.
		const float Alpha = (ElapsedNative - FadeIn - HoldTime) / FMath::Max(FadeOut, KINDA_SMALL_NUMBER);
		Opacity = 1.f - FMath::Clamp(Alpha, 0.f, 1.f);
	}

	SetRenderOpacity(Opacity);
	SetRenderTranslation(FVector2D(0.f, OffsetY));
}
