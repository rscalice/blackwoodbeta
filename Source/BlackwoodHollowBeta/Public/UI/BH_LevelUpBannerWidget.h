// Blackwood Hollow - level-up banner widget (Phase 9)
// Target: Unreal Engine 5.8 (C++), UMG
//
// Parent of WBP_LevelUpBanner. Widget Blueprints bind by widget NAME:
//   LevelText     UTextBlock       (required)  "LEVEL 3"
//   StatsText     UTextBlock       (optional)  "+12 Health · +4 Posture · +3 Stamina" (rounded; zero gains skipped; hidden when empty)
//   Anim_Show     UWidgetAnimation (optional)  animate in, hold, animate out; the widget collapses when it finishes
//
// UBH_HUDWidget::HandleLevelUpDetailed calls ShowLevelUp when the HUD has this widget bound as LevelUpBanner.
// Starts Collapsed and is never hit-testable. A level up arriving while the banner is up restarts it.
//
// With no Anim_Show a native fallback runs in NativeTick: fade in (FadeIn) while sliding down from above, hold (HoldTime), fade out
// (FadeOut), then collapse.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Progression/BH_LevelUpInfo.h"
#include "BH_LevelUpBannerWidget.generated.h"

class UTextBlock;
class UWidgetAnimation;

UCLASS(Abstract, Blueprintable)
class BLACKWOODHOLLOWBETA_API UBH_LevelUpBannerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Shows (or restarts) the banner for this level up. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|LevelUp")
	void ShowLevelUp(const FBH_LevelUpInfo& Info);

	/** Hides the banner now (stops Anim_Show / the native fallback and collapses). */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|LevelUp")
	void HideBanner();

	// -- Native fallback timing (ignored when Anim_Show is bound) -----------------------------------------------------

	/** Seconds to fade in (and slide down). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|LevelUp", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float FadeIn = 0.25f;

	/** Seconds fully visible. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|LevelUp", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float HoldTime = 2.5f;

	/** Seconds to fade out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|LevelUp", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float FadeOut = 0.5f;

	/** Pixels the banner starts above its resting position and slides down while fading in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|LevelUp", meta = (ClampMin = "0.0"))
	float SlideDistance = 24.f;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** Extra flourishes (sound, particles, a material pulse ...) for a Blueprint child. Runs right after the texts are set and playback starts. */
	UFUNCTION(BlueprintImplementableEvent, Category = "BlackwoodHollow|LevelUp", meta = (DisplayName = "On Shown"))
	void OnShown(const FBH_LevelUpInfo& Info);

	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|LevelUp", meta = (BindWidget))
	TObjectPtr<UTextBlock> LevelText;

	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|LevelUp", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatsText;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "BlackwoodHollow|LevelUp", meta = (BindWidgetAnimOptional))
	TObjectPtr<UWidgetAnimation> Anim_Show;

private:
	UFUNCTION()
	void HandleShowAnimFinished();

	/** "+12 Health · +4 Posture · +3 Stamina": rounded, zero (or negative) entries skipped. Empty when nothing gained. */
	static FText BuildStatsText(const FBH_LevelUpInfo& Info);

	/** Native fallback applied at ElapsedNative seconds. */
	void ApplyNativeFrame();

	bool bAnimFinishedBound = false;
	bool bNativeActive = false;
	float ElapsedNative = 0.f;
};
