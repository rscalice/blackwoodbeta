// Blackwood Hollow - first-time hint banner (Phase 13E)
// Target: Unreal Engine 5.8 (C++)
//
// Native base of WBP_HintBanner, the presenter for hints. It sits in the SAME HUD slot as the objective tracker (WBP_QuestTracker,
// top centre under the compass) and takes turns with it: the objective fades out (UBH_HintSettings::TrackerSwapSeconds), the hint
// fades in with a small rise, holds, fades out, then the objective fades back in. A hint shows even while the quiet HUD hides the
// tracker in combat. The local UBH_HintSubsystem owns WHEN a hint shows; this widget only presents one. The look (title row with one
// glyph, centred body, soft dark band) lives in the Blueprint, which fills itself in OnHintPresented. Timings come from
// UBH_HintSettings. The widget registers itself with the local player's hint subsystem when constructed.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BH_HintBannerWidget.generated.h"

class UBH_HintData;
class UInputAction;

UCLASS(Abstract, Blueprintable)
class BLACKWOODHOLLOWBETA_API UBH_HintBannerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Start presenting a hint. Returns false when one is already showing. */
	bool ShowHint(const UBH_HintData& Hint, float HoldSeconds);

	bool IsShowing() const { return Phase != EPhase::Idle; }

	/** Broadcast when the fade-out has finished. */
	FSimpleMulticastDelegate OnFinished;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** Fill the widget: set the title and body text and the glyph widget (only the first action is passed: one glyph per hint). */
	UFUNCTION(BlueprintImplementableEvent, Category = "BlackwoodHollow|Hints")
	void OnHintPresented(const FText& Title, const FText& Body, const TArray<UInputAction*>& GlyphActions);

private:
	enum class EPhase : uint8 { Idle, TrackerOut, FadeIn, Hold, FadeOut, TrackerIn };

	void Finish();
	void EnterPhase(EPhase NewPhase);
	/** Finds this player's objective tracker (WBP_QuestTracker) the first time a hint shows. */
	UWidget* FindTracker();
	/** The tracker's own render opacity; the quiet-HUD fade uses its colour tint, so the two multiply and never fight. */
	void SetTrackerOpacity(float Alpha);

	EPhase Phase = EPhase::Idle;
	float PhaseTime = 0.f;
	float HoldTime = 0.f;
	TWeakObjectPtr<UWidget> Tracker;
};
