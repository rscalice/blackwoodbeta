// Blackwood Hollow - first-time hint banner (Phase 13E)
// Target: Unreal Engine 5.8 (C++)
//
// Native base of WBP_HintBanner, the lower-middle presenter for hints (no box: title, one line of body, inline input glyphs). The
// local UBH_HintSubsystem owns WHEN a hint shows; this widget only presents one: fade in with a small rise, hold, fade out. The look
// (text blocks, glyph widgets, gradient band) lives in the Blueprint, which fills itself in OnHintPresented. Timings come from
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

	/** Fill the widget: set the title and body text and one glyph widget per action (up to three; hide the rest). */
	UFUNCTION(BlueprintImplementableEvent, Category = "BlackwoodHollow|Hints")
	void OnHintPresented(const FText& Title, const FText& Body, const TArray<UInputAction*>& GlyphActions);

private:
	enum class EPhase : uint8 { Idle, FadeIn, Hold, FadeOut };

	void Finish();

	EPhase Phase = EPhase::Idle;
	float PhaseTime = 0.f;
	float HoldTime = 0.f;
};
