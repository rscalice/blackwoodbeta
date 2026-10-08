// Blackwood Hollow - XP bar widget (Phase 9)
// Target: Unreal Engine 5.8 (C++), UMG
//
// Parent of WBP_XPBar. Widget Blueprints bind by widget NAME:
//   XPFill        UProgressBar   (required)  the XP fill, 0..1 toward the next level
//   LevelText     UTextBlock     (required)  "Lv 3"
//   XPGainText    UTextBlock     (optional)  "+20 XP" after a gain
//   Anim_XPGain   UWidgetAnimation (optional) plays on every XP gain; when it is bound the native fade of XPGainText is skipped
//
// The widget finds the owning player's UBH_ProgressionComponent (PlayerState) by itself: PlayerState may not exist yet when the widget
// is constructed, so it retries on a short timer until it is found and then keeps checking now and then, so a PlayerState / component
// that changes (travel, respawn of the PlayerState) is rebound. Values are initialised from the current state; the fill tweens
// toward the target. On a level up the fill runs to full, wraps to 0 and continues to the new value.
//
// Idle dim: the whole widget fades to IdleOpacity after IdleDelay seconds without an XP change and back to 1 on any change.
// Place it in WBP_HUD_Main under the name XPBar (UBH_HUDWidget::XPBar).

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Engine/TimerHandle.h"
#include "BH_XPBarWidget.generated.h"

class UBH_ProgressionComponent;
class UProgressBar;
class UTextBlock;
class UWidgetAnimation;

UCLASS(Abstract, Blueprintable)
class BLACKWOODHOLLOWBETA_API UBH_XPBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Fill tween speed (FInterpTo rate: higher = faster). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|XP", meta = (ClampMin = "0.1"))
	float InterpSpeed = 6.f;

	/** Seconds the "+N XP" text stays up in the native fallback (no Anim_XPGain). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|XP", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float GainTextDuration = 1.5f;

	/** Length of the last part of GainTextDuration spent fading the text out (native fallback). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|XP", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float GainTextFadeTime = 0.35f;

	/** Seconds without an XP change before the widget dims. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|XP", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float IdleDelay = 4.f;

	/** Render opacity while dimmed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|XP", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float IdleOpacity = 0.5f;

	/** Dim / un-dim fade speed (FInterpTo rate). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|XP", meta = (ClampMin = "0.1"))
	float OpacityInterpSpeed = 4.f;

	/** Seconds between attempts to find the progression component while there is none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|XP", meta = (ClampMin = "0.05", ForceUnits = "s"))
	float BindRetryInterval = 0.25f;

	/** Seconds between checks that the bound component is still the owning player's (rebinding when it changed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|XP", meta = (ClampMin = "0.1", ForceUnits = "s"))
	float RebindCheckInterval = 2.f;

	/** Looks for the owning player's progression component right now and (re)binds if it differs from the bound one. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|XP")
	void TryBindProgression();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|XP", meta = (BindWidget))
	TObjectPtr<UProgressBar> XPFill;

	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|XP", meta = (BindWidget))
	TObjectPtr<UTextBlock> LevelText;

	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|XP", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> XPGainText;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "BlackwoodHollow|XP", meta = (BindWidgetAnimOptional))
	TObjectPtr<UWidgetAnimation> Anim_XPGain;

private:
	UFUNCTION()
	void HandleXPChanged(int32 NewCurrentXP, int32 XPToNextLevel);

	void BindTo(UBH_ProgressionComponent* Progression);
	void Unbind();
	void ScheduleBindTimer(float Interval);

	/** Reads the component. bInitial: take the values as they are (no gain text, no wrap). */
	void Refresh(bool bInitial);
	void UpdateLevelText(int32 Level);
	void ShowGain(int32 Delta);
	void ApplyFill();

	TWeakObjectPtr<UBH_ProgressionComponent> BoundProgression;
	FTimerHandle BindTimer;
	float BindTimerInterval = 0.f;

	bool bInitialised = false;
	int32 LastLevel = 0;
	int32 LastXP = 0;

	/** Fill shown right now / where it is heading (0..1). */
	float DisplayFill = 0.f;
	float TargetFill = 0.f;
	float AppliedFill = -1.f;
	/** A level up is pending: run to full, then wrap to 0 and continue to TargetFill. */
	bool bWrapPending = false;

	/** Seconds since the last XP change (idle dim) and the opacity currently applied. */
	float IdleTimer = 0.f;
	float CurrentOpacity = 1.f;

	/** Native "+N XP" fallback: seconds since it was shown, negative = hidden. */
	float GainTextAge = -1.f;
};
