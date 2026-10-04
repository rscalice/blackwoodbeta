// Blackwood Hollow - boss health bar (bottom-centre, Souls-style)
// Target: Unreal Engine 5.8 (C++), UMG, GAS
//
// WBP_BossHealthBar's parent. Widget Blueprints bind by widget NAME (BindWidgetOptional), same convention as BH_HUDElements.h:
//   Txt_Name        boss name (UBH_CombatIdentityComponent::GetBossBarName)
//   Img_HealthTrail pale "damage chunk" bar behind the health fill; holds the old value for TrailDelay seconds, then catches up
//   Img_HealthBar   crimson health fill (eased)
//   Img_PostureBar / Img_Brackets / Row_Posture / Bar_Posture   thin posture bar underneath, same FBH_PostureBarPresenter as WBP_TargetVitals
//
// SLANT: the bars are the M_UI_SlantedBar parallelogram. Its ShearDegrees scalar is the slant; this widget writes SlantDegrees
// (default -20) into every bar's material instance, so the -20 degree look does not depend on how the MI was authored.
//
// LIFETIME: UBH_HUDSubsystem owns one instance per local player and decides when it is shown (a living boss that is aggroed on the
// local pawn). PresentBoss binds it to that boss's ASC and fades in; DismissBar fades out and unbinds. The widget never polls the world.

#pragma once

#include "CoreMinimal.h"
#include "UI/BH_HUDWidget.h"
#include "UI/BH_HUDElements.h"
#include "Widgets/SWidget.h"
#include "BH_BossHealthBarWidget.generated.h"

class UImage;
class UTextBlock;
class UProgressBar;
class UMaterialInterface;
class UMaterialInstanceDynamic;

UCLASS(Abstract, Blueprintable)
class BLACKWOODHOLLOWBETA_API UBH_BossHealthBarWidget : public UBH_HUDWidget
{
	GENERATED_BODY()

public:
	UBH_BossHealthBarWidget(const FObjectInitializer& ObjectInitializer);

	/** Binds the bar to Boss (its ASC + name) and fades it in. Safe to call again for another boss. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|HUD")
	void PresentBoss(AActor* Boss);

	/** Fades the bar out (then collapses and unbinds). */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|HUD")
	void DismissBar();

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|HUD")
	AActor* GetBossActor() const { return BossActor.Get(); }

	/** True from PresentBoss until DismissBar (the fade is not part of this). */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|HUD")
	bool IsBarShown() const { return bShown; }

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|HUD")
	float GetBarOpacity() const { return CurrentOpacity; }

	/** Eased health fill, 0..1 (what Img_HealthBar shows). */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|HUD")
	float GetDisplayedHealthPercent() const { return DisplayedHealthPercent; }

	/** Health the bar is easing toward, 0..1. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|HUD")
	float GetTargetHealthPercent() const { return TargetHealthPercent; }

	/** Damage-chunk trail, 0..1 (never below the displayed health). */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|HUD")
	float GetTrailPercent() const { return TrailPercent; }

	/** Boss name currently shown. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|HUD")
	FText GetShownName() const { return ShownName; }

	// -- Tunables ---------------------------------------------------------------------

	/** Slant of the bar parallelograms in degrees (written to the materials' ShearDegrees; -20 = top edge leans left). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|HUD|Boss")
	float SlantDegrees = -20.f;

	/** How fast the health fill eases to its new value (per second, exponential). 0 = snap. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|HUD|Boss", meta = (ClampMin = "0.0"))
	float HealthEaseSpeed = 12.f;

	/** Seconds the damage trail holds the old value after a hit before it starts catching up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|HUD|Boss", meta = (ClampMin = "0.0"))
	float TrailDelay = 0.6f;

	/** Trail catch-up speed in bar fractions per second (0.5 = a full bar in 2 s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|HUD|Boss", meta = (ClampMin = "0.01"))
	float TrailCatchUpSpeed = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|HUD|Boss", meta = (ClampMin = "0.01"))
	float FadeInTime = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|HUD|Boss", meta = (ClampMin = "0.01"))
	float FadeOutTime = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|HUD|Boss")
	FName PercentParameterName = TEXT("Percent");

	/** Posture colours, near-break danger and break punch (shared with the target's posture bar). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BlackwoodHollow|HUD|Boss")
	FBH_PostureBarStyle PostureStyle;

	// -- Bar materials (M_UI_SlantedBar instances); assigned to the image brushes on construct when set ---------------
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BlackwoodHollow|HUD|Boss|Materials")
	TObjectPtr<UMaterialInterface> HealthBarMaterial;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BlackwoodHollow|HUD|Boss|Materials")
	TObjectPtr<UMaterialInterface> TrailBarMaterial;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BlackwoodHollow|HUD|Boss|Materials")
	TObjectPtr<UMaterialInterface> PostureBarMaterial;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BlackwoodHollow|HUD|Boss|Materials")
	TObjectPtr<UMaterialInterface> BracketsMaterial;

protected:
	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Name;

	/** Pale damage-chunk bar (behind the health fill). */
	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UImage> Img_HealthTrail;

	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UImage> Img_HealthBar;

	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UImage> Img_PostureBar;

	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UImage> Img_Brackets;

	/** Whole posture row (bar + brackets); its render scale X expands in danger. Falls back to the bar itself. */
	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> Row_Posture;

	/** Legacy plain progress bar, used only when Img_PostureBar is absent. */
	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> Bar_Posture;

	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void HandleHealthChanged(float Health, float MaxHealth) override;
	virtual void HandlePostureChanged(float Posture, float MaxPosture) override;
	virtual void HandlePostureBrokenChanged(bool bBroken) override;

private:
	/** Brushes get their materials, MIDs are created and the slant written (idempotent). */
	void EnsureBarMaterials();
	void ApplyBarPercents();
	void SyncAspectRatio(UImage* Image, UMaterialInstanceDynamic* MID, FVector2D& LastSize);

	/** Fade, easing, trail and posture update. Driven by NativeTick AND a Slate active timer (a widget class with no Blueprint tick
	 *  event is not always given a native tick under TickFrequency Auto); whichever runs first in a frame wins. */
	void UpdateBar(float InDeltaTime);
	EActiveTimerReturnType OnActiveTimer(double InCurrentTime, float InDeltaTime);
	void EnsureActiveTimer();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HealthBarMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> TrailBarMID;

	FBH_PostureBarPresenter Presenter;
	TWeakObjectPtr<AActor> BossActor;
	FText ShownName;

	float TargetHealthPercent = 1.f;
	float DisplayedHealthPercent = 1.f;
	float TrailPercent = 1.f;
	float TrailHoldLeft = 0.f;
	bool bHealthSeeded = false;

	float CurrentOpacity = 0.f;
	bool bShown = false;

	FVector2D LastHealthSize = FVector2D::ZeroVector;
	FVector2D LastTrailSize = FVector2D::ZeroVector;

	TSharedPtr<FActiveTimerHandle> ActiveTimerHandle;
	uint64 LastUpdateFrame = MAX_uint64;
};
