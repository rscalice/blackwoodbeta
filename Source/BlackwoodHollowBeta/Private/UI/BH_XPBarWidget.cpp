// Blackwood Hollow - XP bar widget (implementation)

#include "UI/BH_XPBarWidget.h"
#include "Progression/BH_ProgressionComponent.h"
#include "Progression/BH_RPGSettings.h"
#include "Animation/WidgetAnimation.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "TimerManager.h"

namespace
{
	/** While a level up is pending the fill aims this far past full, so it crosses 1.0 in a decent time instead of crawling toward it. */
	constexpr float WrapOvershoot = 0.15f;

	/** Percent changes smaller than this are not pushed to the progress bar. */
	constexpr float FillApplyTolerance = 0.0005f;
}

void UBH_XPBarWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (IsDesignTime())
	{
		return;
	}

	bInitialised = false;
	AppliedFill = -1.f;
	GainTextAge = -1.f;
	IdleTimer = 0.f;
	CurrentOpacity = 1.f;
	SetRenderOpacity(1.f);

	if (XPGainText)
	{
		XPGainText->SetRenderOpacity(0.f); // hidden until the first gain (an Anim_XPGain fades it in itself)
	}
	if (XPFill)
	{
		XPFill->SetPercent(0.f);
	}

	TryBindProgression();
}

void UBH_XPBarWidget::NativeDestruct()
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

void UBH_XPBarWidget::TryBindProgression()
{
	// The PlayerState may not exist yet (or may be replaced): look it up every time rather than caching it.
	const APlayerController* PC = GetOwningPlayer();
	const APlayerState* PlayerState = PC ? PC->GetPlayerState<APlayerState>() : nullptr;
	UBH_ProgressionComponent* Found = UBH_ProgressionComponent::FindProgression(PlayerState);

	if (Found != BoundProgression.Get())
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

	// Fast retries while nothing is bound, a slow sanity check once it is.
	ScheduleBindTimer(BoundProgression.IsValid() ? RebindCheckInterval : BindRetryInterval);
}

void UBH_XPBarWidget::ScheduleBindTimer(float Interval)
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
	Timers.SetTimer(BindTimer, this, &UBH_XPBarWidget::TryBindProgression, Interval, true);
}

void UBH_XPBarWidget::BindTo(UBH_ProgressionComponent* Progression)
{
	Unbind();
	if (!Progression)
	{
		return;
	}
	BoundProgression = Progression;
	Progression->OnXPChanged.AddUniqueDynamic(this, &UBH_XPBarWidget::HandleXPChanged);

	// Initialise from the current values: no gain text, no wrap.
	bInitialised = false;
	Refresh(/*bInitial*/ true);
}

void UBH_XPBarWidget::Unbind()
{
	if (UBH_ProgressionComponent* Progression = BoundProgression.Get())
	{
		Progression->OnXPChanged.RemoveDynamic(this, &UBH_XPBarWidget::HandleXPChanged);
	}
	BoundProgression.Reset();
	bInitialised = false;
}

// ----------------------------------------------------------------------------
// Values
// ----------------------------------------------------------------------------

void UBH_XPBarWidget::HandleXPChanged(int32 NewCurrentXP, int32 XPToNextLevel)
{
	// The arguments are not used on purpose: level and XP are read together from the component so a level up (XP wrapped to a
	// small number at a new level) is seen as one change.
	Refresh(/*bInitial*/ false);
}

void UBH_XPBarWidget::Refresh(bool bInitial)
{
	const UBH_ProgressionComponent* Progression = BoundProgression.Get();
	if (!Progression)
	{
		return;
	}

	const int32 Level = Progression->GetLevel();
	const int32 XP = Progression->GetCurrentXP();
	const float Progress = Progression->GetXPProgress01();

	if (bInitial || !bInitialised)
	{
		LastLevel = Level;
		LastXP = XP;
		DisplayFill = Progress;
		TargetFill = Progress;
		bWrapPending = false;
		bInitialised = true;
		IdleTimer = 0.f;
		UpdateLevelText(Level);
		ApplyFill();
		return;
	}

	if (Level == LastLevel && XP == LastXP)
	{
		TargetFill = Progress; // e.g. the XP needed for the level changed; not an XP change, so no un-dim
		return;
	}

	int32 Gained = 0;
	if (Level > LastLevel)
	{
		// Run to full, wrap, continue to the new value. XP gained = what was left of the old levels plus what is banked now.
		bWrapPending = true;
		Gained = XP - LastXP;
		if (const UBH_RPGSettings* Settings = UBH_RPGSettings::Get())
		{
			for (int32 L = LastLevel; L < Level; ++L)
			{
				Gained += Settings->GetXPToNextLevel(L);
			}
		}
	}
	else if (Level < LastLevel)
	{
		// Level set downward (cheat / reset): jump, nothing to celebrate.
		bWrapPending = false;
		DisplayFill = Progress;
	}
	else
	{
		Gained = XP - LastXP;
	}

	if (Level != LastLevel)
	{
		UpdateLevelText(Level);
	}
	LastLevel = Level;
	LastXP = XP;
	TargetFill = Progress;
	IdleTimer = 0.f; // any XP change un-dims

	if (Gained > 0)
	{
		ShowGain(Gained);
	}
}

void UBH_XPBarWidget::UpdateLevelText(int32 Level)
{
	if (LevelText)
	{
		LevelText->SetText(FText::Format(NSLOCTEXT("BHXP", "LevelFmt", "Lv {0}"), FText::AsNumber(Level)));
	}
}

void UBH_XPBarWidget::ShowGain(int32 Delta)
{
	if (XPGainText)
	{
		XPGainText->SetText(FText::Format(NSLOCTEXT("BHXP", "GainFmt", "+{0} XP"), FText::AsNumber(Delta)));
	}

	if (Anim_XPGain)
	{
		// Restart from the beginning on every gain.
		StopAnimation(Anim_XPGain);
		PlayAnimation(Anim_XPGain);
	}
	else if (XPGainText)
	{
		GainTextAge = 0.f;
		XPGainText->SetRenderOpacity(1.f);
	}
}

void UBH_XPBarWidget::ApplyFill()
{
	if (!XPFill)
	{
		return;
	}
	const float Percent = FMath::Clamp(DisplayFill, 0.f, 1.f);
	if (AppliedFill < 0.f || !FMath::IsNearlyEqual(Percent, AppliedFill, FillApplyTolerance))
	{
		AppliedFill = Percent;
		XPFill->SetPercent(Percent);
	}
}

// ----------------------------------------------------------------------------
// Tick: fill tween, "+N XP" fallback, idle dim
// ----------------------------------------------------------------------------

void UBH_XPBarWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// Fill tween.
	if (bInitialised)
	{
		const float Goal = bWrapPending ? 1.f + WrapOvershoot : TargetFill;
		DisplayFill = FMath::FInterpTo(DisplayFill, Goal, InDeltaTime, InterpSpeed);
		if (bWrapPending && DisplayFill >= 1.f)
		{
			DisplayFill = 0.f; // full: wrap and carry on toward TargetFill
			bWrapPending = false;
		}
		ApplyFill();
	}

	// Native "+N XP" fallback (an Anim_XPGain drives the text itself).
	if (!Anim_XPGain && XPGainText && GainTextAge >= 0.f)
	{
		GainTextAge += InDeltaTime;
		if (GainTextAge >= GainTextDuration)
		{
			GainTextAge = -1.f;
			XPGainText->SetRenderOpacity(0.f);
		}
		else
		{
			const float FadeTime = FMath::Max(GainTextFadeTime, KINDA_SMALL_NUMBER);
			const float FadeStart = FMath::Max(GainTextDuration - FadeTime, 0.f);
			const float Alpha = GainTextAge < FadeStart ? 1.f : 1.f - (GainTextAge - FadeStart) / FadeTime;
			XPGainText->SetRenderOpacity(FMath::Clamp(Alpha, 0.f, 1.f));
		}
	}

	// Idle dim: smooth fade down after IdleDelay without a change, back up on any change.
	IdleTimer += InDeltaTime;
	const float TargetOpacity = IdleTimer >= IdleDelay ? IdleOpacity : 1.f;
	CurrentOpacity = FMath::FInterpTo(CurrentOpacity, TargetOpacity, InDeltaTime, OpacityInterpSpeed);
	if (!FMath::IsNearlyEqual(GetRenderOpacity(), CurrentOpacity, 0.001f))
	{
		SetRenderOpacity(CurrentOpacity);
	}
}
