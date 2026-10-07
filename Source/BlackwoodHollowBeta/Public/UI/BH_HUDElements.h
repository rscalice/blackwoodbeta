// Blackwood Hollow - Production HUD elements
// Target: Unreal Engine 5.8 (C++), UMG
//
// Widget Blueprints bind to these by widget NAME (BindWidgetOptional), so the
// layout and art live in the WBP and the behaviour lives here:
//   UBH_VitalsClusterWidget     -> WBP_VitalsCluster (Img_HealthBar | Bar_Health, Txt_Health (+Txt_HealthFill, Clip_HealthFill),
//                                  Img_StaminaBar | Bar_Stamina, Txt_Stamina (+Txt_StaminaFill, Clip_StaminaFill), Img_StanceEmblem)
//   UBH_ContextualPostureWidget -> WBP_ContextualPosture (Img_PostureBar | Bar_Posture, Img_Brackets)
//   UBH_TargetVitalsWidget      -> WBP_TargetVitals (Txt_Name, Img_HealthBar, Txt_Health (+Txt_HealthFill, Clip_HealthFill),
//                                  Img_PostureBar | Bar_Posture, Img_Brackets, Row_Posture)
//   WBP_HUD_Main is a plain UBH_HUDWidget that nests them.
//
// Two shared, non-UObject helpers keep the behaviour in ONE place:
//   FBH_TwoToneRatioText     - "cur / max" text drawn light over the empty track and dark over the fill.
//   FBH_PostureBarPresenter  - posture fill + corner brackets + near-break danger + break punch.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UI/BH_HUDWidget.h"
#include "BH_HUDElements.generated.h"

class UProgressBar;
class UTextBlock;
class UImage;
class UTexture2D;
class UMaterialInstanceDynamic;
class UWidget;

// ============================================================================
// Two-tone ratio text
// ============================================================================

/** Colours for the two copies of a ratio text. Light = over the empty track, Dark = over the filled part. */
USTRUCT(BlueprintType)
struct BLACKWOODHOLLOWBETA_API FBH_TwoToneTextStyle
{
	GENERATED_BODY()

	/** Text colour where the text overlaps the EMPTY track (ghost white). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "TwoTone")
	FLinearColor LightColor = FLinearColor(0.92f, 0.92f, 0.95f, 1.f);

	/** Outline for the light copy (dark, for contrast on the track / world behind it). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "TwoTone")
	FLinearColor LightOutlineColor = FLinearColor(0.06f, 0.05f, 0.05f, 0.9f);

	/** Text colour where the text overlaps the FILLED part (dark). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "TwoTone")
	FLinearColor DarkColor = FLinearColor(0.06f, 0.05f, 0.05f, 1.f);

	/** Outline for the dark copy (ghost white, as the original single-colour style). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "TwoTone")
	FLinearColor DarkOutlineColor = FLinearColor(0.92f, 0.92f, 0.95f, 0.9f);

	/** Shear the clip (and counter-shear the text) so the colour split follows the bar material's slant. False = vertical split. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "TwoTone")
	bool bFollowBarSlant = true;

	/** Multiplies the slant angle (1 = exactly the material's slant; -1 flips direction if a material is authored the other way). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "TwoTone")
	float SlantSign = 1.f;
};

/**
 * Reusable UMG pattern (plain struct owned by a widget): two identical TextBlocks at the same place.
 *   Light : full width, bottom layer.
 *   Dark  : inside Clip (a SizeBox with ClipToBounds, left anchored) -> Canvas -> Fixed overlay (sized to the FULL bar) -> Dark text,
 *           so both copies are laid out identically. The clip's width = bar width x fill, sheared like the bar material;
 *           the fixed overlay is counter-sheared so the dark text is drawn upright.
 * Missing Dark/Clip degrades to a single text in the dark style.
 */
struct FBH_TwoToneRatioText
{
	/** Wire the widgets and apply the style (copies the light text's font size to the dark copy). Safe to call with nulls. */
	void Bind(UTextBlock* InLight, UTextBlock* InDark, UWidget* InClip, const FBH_TwoToneTextStyle& InStyle);

	bool IsBound() const { return Light.IsValid(); }
	bool HasSplit() const { return Dark.IsValid() && Clip.IsValid(); }

	/** The single place the string is built, so both copies can never diverge. */
	static FText FormatRatio(float Current, float Max);

	void SetRatio(float Current, float Max, bool bVisible);

	/** Lay the split out: BarSize in local units, FillPercent 0..1, ShearDegrees = UMG render shear matching the bar slant. */
	void UpdateSplit(const FVector2D& BarSize, float FillPercent, float ShearDegrees);

	/** UMG render shear (degrees) that matches a slanted-bar material instance (ShearDegrees / AspectRatio params) at BarSize. */
	static float ComputeShearDegrees(const UMaterialInstanceDynamic* BarMID, const FVector2D& BarSize, float SlantSign);

private:
	TWeakObjectPtr<UTextBlock> Light;
	TWeakObjectPtr<UTextBlock> Dark;
	TWeakObjectPtr<UWidget> Clip;
	FBH_TwoToneTextStyle Style;
	FVector2D LastFixedSize = FVector2D::ZeroVector;
	bool bShown = true;
	float LastWidth = -1.f;
	float LastShear = 0.f;
};

// ============================================================================
// Posture bar presentation (shared by the player's and the target's posture bars)
// ============================================================================

USTRUCT(BlueprintType)
struct BLACKWOODHOLLOWBETA_API FBH_PostureBarStyle
{
	GENERATED_BODY()

	// -- Fill colours ---------------------------------------------------------
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Colors")
	FLinearColor NormalFillColor = FLinearColor(0.85f, 0.66f, 0.24f, 1.f);

	/** Fill colour alternates between this and FlashFillColor while posture is broken. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Colors")
	FLinearColor BrokenFillColor = FLinearColor(0.72f, 0.12f, 0.08f, 1.f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Colors")
	FLinearColor FlashFillColor = FLinearColor(1.f, 0.93f, 0.75f, 1.f);

	/** Flashes per second while broken. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Colors", meta = (ClampMin = "0.1"))
	float FlashRate = 4.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Colors")
	FLinearColor NormalBracketColor = FLinearColor(0.92f, 0.85f, 0.65f, 0.85f);

	// -- Corner brackets (drive M_UI_CornerBrackets' Thickness / ArmLength / ArmLengthV from pixel sizes; 0 = keep the material defaults) --
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Brackets", meta = (ClampMin = "0.0"))
	float BracketThicknessPx = 2.f;

	/** Horizontal arm length in pixels. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Brackets", meta = (ClampMin = "0.0"))
	float BracketArmPx = 28.f;

	/** Vertical arm length in pixels (capped at 45% of the bracket height). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Brackets", meta = (ClampMin = "0.0"))
	float BracketVerticalArmPx = 10.f;

	// -- Near-break danger ------------------------------------------------------
	/** Posture lost fraction (0..1, fills toward break) at/above which the bar is in danger: 0.8 = within 20% of breaking. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Danger", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DangerThreshold = 0.8f;

	/** Fill and brackets lerp to this in danger. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Danger")
	FLinearColor DangerColor = FLinearColor(0.8f, 0.05f, 0.05f, 1.f);

	/** Bracket glow pulses per second while in danger. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Danger", meta = (ClampMin = "0.0"))
	float DangerPulseHz = 3.f;

	/** Render scale X of the whole bar while in danger. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Danger", meta = (ClampMin = "1.0"))
	float DangerScaleX = 1.08f;

	/** Seconds to ease into / out of the danger state (colour + scale). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Danger", meta = (ClampMin = "0.01"))
	float DangerEaseTime = 0.15f;

	// -- Break punch ------------------------------------------------------------
	/** Render scale X reached the moment posture breaks; it then settles back. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Break", meta = (ClampMin = "1.0"))
	float BreakPunchScaleX = 1.2f;

	/** Seconds for the punch to settle. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Break", meta = (ClampMin = "0.01"))
	float BreakPunchDuration = 0.3f;
};

/**
 * Drives one posture bar: the fill (Img_PostureBar MID of MI_UI_SlantedBar_Posture, else a ProgressBar),
 * the corner brackets (Img_Brackets, M_UI_CornerBrackets), the near-break danger look, and the break flash + punch.
 * The owning widget feeds it the lost-posture fraction (fills toward break) and the broken flag and calls Tick.
 */
struct FBH_PostureBarPresenter
{
	/** ScaleTarget = widget whose render scale X expands (the whole bar). Any argument may be null. */
	void Bind(UImage* InBar, UImage* InBrackets, UProgressBar* InFallbackBar, UWidget* InScaleTarget);

	/** Back to the idle look (no danger, no punch, scale 1). */
	void Reset();

	void SetDamageFraction(float InFraction) { DamageFraction = FMath::Clamp(InFraction, 0.f, 1.f); }
	float GetDamageFraction() const { return DamageFraction; }
	void SetBroken(bool bInBroken);
	bool IsInDanger() const { return DangerAlpha > 0.f; }
	float GetCurrentScaleX() const { return CurrentScaleX; }

	void Tick(float DeltaTime, const FBH_PostureBarStyle& Style);

private:
	void SyncGeometryParams(const FBH_PostureBarStyle& Style);

	TWeakObjectPtr<UImage> BarImage;
	TWeakObjectPtr<UImage> BracketImage;
	TWeakObjectPtr<UProgressBar> FallbackBar;
	TWeakObjectPtr<UWidget> ScaleTarget;
	TWeakObjectPtr<UMaterialInstanceDynamic> BarMID;
	TWeakObjectPtr<UMaterialInstanceDynamic> BracketMID;

	float DamageFraction = 0.f;
	bool bBroken = false;
	float DangerAlpha = 0.f;
	float PulseTime = 0.f;
	float FlashTime = 0.f;
	float PunchAlpha = 0.f;
	float CurrentScaleX = 1.f;
	FVector2D LastBarSize = FVector2D::ZeroVector;
	FVector2D LastBracketSize = FVector2D::ZeroVector;
};

// ============================================================================
// Widgets
// ============================================================================

/** Lower-left vitals: health bar, (optional) stamina bar + active stance emblem. */
UCLASS(Abstract, Blueprintable)
class BLACKWOODHOLLOWBETA_API UBH_VitalsClusterWidget : public UBH_HUDWidget
{
	GENERATED_BODY()

public:
	/** Enum_OverlayPose display name -> emblem texture. Poses without an entry hide the emblem. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|HUD")
	TMap<FString, TObjectPtr<UTexture2D>> StanceIcons;

	/** Tag-keyed twin of StanceIcons (Stance.Weapon.*); read first, the legacy map is the fallback. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|HUD", meta = (Categories = "Stance.Weapon"))
	TMap<FGameplayTag, TObjectPtr<UTexture2D>> StanceIconsByTag;

	/** Show "cur / max" over the health bar. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|HUD")
	bool bShowHealthText = true;

	/** How fast the health fill eases to its new value (per second, exponential). 0 = snap. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|HUD", meta = (ClampMin = "0.0"))
	float HealthEaseSpeed = 10.f;

	/** Show "cur / max" over the stamina bar. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|HUD")
	bool bShowStaminaText = false;

	/** How fast the stamina fill eases to its new value (per second, exponential). 0 = snap. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|HUD", meta = (ClampMin = "0.0"))
	float StaminaEaseSpeed = 10.f;

	/** Scalar parameter (0..1) on the bar materials that is driven with the eased fill amount. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|HUD")
	FName PercentParameterName = TEXT("Percent");

	/** Light/dark colours of the ratio text (light over the empty track, dark over the fill). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BlackwoodHollow|HUD")
	FBH_TwoToneTextStyle RatioTextStyle;

protected:
	/**
	 * Preferred bars: an Image whose brush is a material (e.g. MI_UI_SlantedBar_Health) exposing a
	 * scalar "Percent". A dynamic material instance is created from the brush and driven every tick.
	 * When these aren't bound the ProgressBar versions below are used instead.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UImage> Img_HealthBar;

	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UImage> Img_StaminaBar;

	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> Bar_Health;

	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> Bar_Stamina;

	/** Light copy of the ratio text (full width, bottom layer). */
	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Stamina;

	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Health;

	/** Dark copy, inside Clip_HealthFill (clipped to the filled part). */
	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_HealthFill;

	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_StaminaFill;

	/** SizeBox with ClipToBounds, left anchored; its width is driven to BarWidth x fill. */
	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> Clip_HealthFill;

	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> Clip_StaminaFill;

	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UImage> Img_StanceEmblem;

	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void HandleHealthChanged(float Health, float MaxHealth) override;
	virtual void HandleStaminaChanged(float Stamina, float MaxStamina) override;
	virtual void HandleStanceChanged(const FString& StanceName) override;

private:
	/** Creates the dynamic material instances for the Img_* bars (idempotent). */
	void EnsureBarMaterials();
	void ApplyHealthPercent();
	void ApplyStaminaPercent();
	void RefreshHealthSplit();
	void RefreshStaminaSplit();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HealthBarMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> StaminaBarMID;

	FBH_TwoToneRatioText HealthText;
	FBH_TwoToneRatioText StaminaText;

	float TargetHealthPercent = 1.f;
	float DisplayedHealthPercent = 1.f;
	float TargetStaminaPercent = 1.f;
	float DisplayedStaminaPercent = 1.f;
	bool bHealthSeeded = false;
	bool bStaminaSeeded = false;
};

/**
 * Reticle-anchored posture bar (Sekiro-style: the fill GROWS as posture is lost).
 * Fades out after posture has been full (and the owner out of combat) for FadeOutDelay,
 * snaps in on any posture loss or while the owner is engaged (blocking / attacking /
 * staggered / parrying), flashes + punches while State.Combat.PostureBroken is active, and
 * warns (crimson, pulsing corner brackets, bar expands) when within the danger threshold of breaking.
 */
UCLASS(Abstract, Blueprintable)
class BLACKWOODHOLLOWBETA_API UBH_ContextualPostureWidget : public UBH_HUDWidget
{
	GENERATED_BODY()

public:
	/** Seconds posture must stay full before the bar starts fading out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|HUD", meta = (ClampMin = "0.0"))
	float FadeOutDelay = 1.0f;

	/** Also keep the bar visible while the owner is blocking / attacking / parrying / staggered (full posture included). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|HUD")
	bool bShowWhileEngaged = true;

	/** Opacity per second while fading out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|HUD", meta = (ClampMin = "0.1"))
	float FadeOutSpeed = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|HUD", meta = (ClampMin = "0.1"))
	float FadeInSpeed = 12.f;

	/** Colours, near-break danger and break punch tunables (shared with the target's posture bar). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BlackwoodHollow|HUD")
	FBH_PostureBarStyle PostureStyle;

	/** Current render scale X of the bar (for debugging / tests). */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|HUD")
	float GetPostureScaleX() const { return Presenter.GetCurrentScaleX(); }

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|HUD")
	bool IsPostureInDanger() const { return Presenter.IsInDanger(); }

protected:
	/** Preferred: slanted bar image (MI_UI_SlantedBar_Posture, "Percent"/"FillColor" params). */
	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UImage> Img_PostureBar;

	/** Corner brackets overlay (M_UI_CornerBrackets). */
	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UImage> Img_Brackets;

	/** Legacy plain progress bar, used only when Img_PostureBar is absent. */
	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> Bar_Posture;

	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void HandlePostureChanged(float Posture, float MaxPosture) override;
	virtual void HandlePostureBrokenChanged(bool bBroken) override;

private:
	FBH_PostureBarPresenter Presenter;
	bool bAtFull = true;
	float TimeAtFull = 0.f;
	float CurrentOpacity = 0.f;
};

/**
 * Lock-on target vitals (top-centre): target name, slanted health bar (Img_HealthBar, Percent param) with a two-tone
 * "cur / max" text, and the posture bar (Img_PostureBar + brackets; fills as posture is LOST - same convention and
 * same danger / break behaviour as UBH_ContextualPostureWidget).
 * Excluded from its parent's InitializeHUD; it binds itself to the locked target's ASC when the lock-on
 * component broadcasts (UBH_HUDWidget::BroadcastLockedTargetChanged) and collapses when the lock is released.
 */
UCLASS(Abstract, Blueprintable)
class BLACKWOODHOLLOWBETA_API UBH_TargetVitalsWidget : public UBH_HUDWidget
{
	GENERATED_BODY()

public:
	UBH_TargetVitalsWidget(const FObjectInitializer& ObjectInitializer);

	/** How fast the health fill eases to its new value (per second, exponential). 0 = snap. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|HUD", meta = (ClampMin = "0.0"))
	float HealthEaseSpeed = 10.f;

	/** Opacity per second while fading in on a new lock. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|HUD", meta = (ClampMin = "0.1"))
	float FadeInSpeed = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|HUD")
	FName PercentParameterName = TEXT("Percent");

	/** Show "cur / max" over the target's health bar. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|HUD")
	bool bShowHealthText = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BlackwoodHollow|HUD")
	FBH_TwoToneTextStyle RatioTextStyle;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BlackwoodHollow|HUD")
	FBH_PostureBarStyle PostureStyle;

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|HUD")
	float GetPostureScaleX() const { return Presenter.GetCurrentScaleX(); }

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|HUD")
	bool IsPostureInDanger() const { return Presenter.IsInDanger(); }

protected:
	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Name;

	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UImage> Img_HealthBar;

	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Health;

	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_HealthFill;

	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> Clip_HealthFill;

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
	virtual void HandleLockedTargetChanged(AActor* Target) override;

private:
	void ApplyHealthPercent();
	void RefreshHealthSplit();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HealthBarMID;

	FBH_TwoToneRatioText HealthText;
	FBH_PostureBarPresenter Presenter;

	float TargetHealthPercent = 1.f;
	float DisplayedHealthPercent = 1.f;
	bool bHealthSeeded = false;
	float CurrentOpacity = 0.f;
	bool bHasTarget = false;
};
