// Blackwood Hollow - overhead enemy vitals widget (health + posture, no text)
// Target: Unreal Engine 5.8 (C++), GAS + UMG
//
// Hosted by UBH_OverheadVitalsComponent (a screen-space widget component on enemy pawns). Parent is UBH_HUDWidget so
// InitializeHUD(ASC) binds Health / MaxHealth / Posture / MaxPosture / PostureBroken exactly like the HUD widgets.
//   WBP_OverheadVitals binds by widget NAME (all optional): Img_HealthBar (material with a "Percent" scalar, optional "FillColor"),
//   Img_PostureBar + Img_Brackets (MI_UI_SlantedBar_Posture / M_UI_CornerBrackets, driven by FBH_PostureBarPresenter - the same
//   look as WBP_TargetVitals: fill grows as posture is LOST, danger pulse, break punch), Row_Posture (scaled on danger / break).
// The component decides WHEN to show and drives the fade (SetOverheadOpacity); this widget only owns the bars.

#pragma once

#include "CoreMinimal.h"
#include "UI/BH_HUDWidget.h"
#include "UI/BH_HUDElements.h"
#include "BH_OverheadVitalsWidget.generated.h"

class UImage;
class UMaterialInstanceDynamic;

UCLASS(Abstract, Blueprintable)
class BLACKWOODHOLLOWBETA_API UBH_OverheadVitalsWidget : public UBH_HUDWidget
{
	GENERATED_BODY()

public:
	UBH_OverheadVitalsWidget(const FObjectInitializer& ObjectInitializer);

	/**
	 * Overall opacity, driven by the owning component every frame (the fade lives in the component's tick, NOT in this
	 * widget's NativeTick: a screen-space widget whose owner is off screen is collapsed by Slate and would never tick).
	 */
	void SetOverheadOpacity(float Opacity);

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|HUD")
	float GetOverheadOpacity() const { return CurrentOpacity; }

	/** How fast the health fill eases to its new value (per second, exponential). 0 = snap. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|HUD", meta = (ClampMin = "0.0"))
	float HealthEaseSpeed = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|HUD")
	FName PercentParameterName = TEXT("Percent");

	/** Health fill colour (thin crimson). Sent to the health material's "FillColor" vector parameter if it has one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|HUD")
	FLinearColor HealthFillColor = FLinearColor(0.62f, 0.06f, 0.07f, 1.f);

	/** Same style struct as the lock-on target's posture bar; defaults here are scaled for a ~140 px bar. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BlackwoodHollow|HUD")
	FBH_PostureBarStyle PostureStyle;

protected:
	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UImage> Img_HealthBar;

	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UImage> Img_PostureBar;

	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UImage> Img_Brackets;

	/** Whole posture row (bar + brackets); its render scale X expands in danger. Falls back to the bar itself. */
	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> Row_Posture;

	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void HandleHealthChanged(float Health, float MaxHealth) override;
	virtual void HandlePostureChanged(float Posture, float MaxPosture) override;
	virtual void HandlePostureBrokenChanged(bool bBroken) override;

private:
	void ApplyHealthPercent();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HealthBarMID;

	FBH_PostureBarPresenter Presenter;

	float TargetHealthPercent = 1.f;
	float DisplayedHealthPercent = 1.f;
	bool bHealthSeeded = false;

	float CurrentOpacity = 0.f;
};
