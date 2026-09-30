// Blackwood Hollow - Production HUD elements
// Target: Unreal Engine 5.8 (C++), UMG
//
// Widget Blueprints bind to these by widget NAME (BindWidgetOptional), so the
// layout and art live in the WBP and the behaviour lives here:
//   UBH_VitalsClusterWidget     -> WBP_VitalsCluster (Img_HealthBar | Bar_Health, Txt_Health, Img_StaminaBar | Bar_Stamina, Txt_Stamina, Img_StanceEmblem)
//   UBH_ContextualPostureWidget -> WBP_ContextualPosture (Bar_Posture)
//   WBP_HUD_Main is a plain UBH_HUDWidget that nests both.

#pragma once

#include "CoreMinimal.h"
#include "UI/BH_HUDWidget.h"
#include "BH_HUDElements.generated.h"

class UProgressBar;
class UTextBlock;
class UImage;
class UTexture2D;
class UMaterialInstanceDynamic;

/** Lower-left vitals: health bar, (optional) stamina bar + active stance emblem. No mana (by design). */
UCLASS(Abstract, Blueprintable)
class BLACKWOODHOLLOWBETA_API UBH_VitalsClusterWidget : public UBH_HUDWidget
{
	GENERATED_BODY()

public:
	/** Enum_OverlayPose display name -> emblem texture. Poses without an entry hide the emblem. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|HUD")
	TMap<FString, TObjectPtr<UTexture2D>> StanceIcons;

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

	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Stamina;

	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Health;

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

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HealthBarMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> StaminaBarMID;

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
 * staggered / parrying), and flashes while State.Combat.PostureBroken is active.
 */
UCLASS(Abstract, Blueprintable)
class BLACKWOODHOLLOWBETA_API UBH_ContextualPostureWidget : public UBH_HUDWidget
{
	GENERATED_BODY()

public:
	/** Seconds posture must stay full before the bar starts fading out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|HUD", meta = (ClampMin = "0.0"))
	float FadeOutDelay = 1.0f;

	/** Opacity per second while fading out / in. */
	/** Also keep the bar visible while the owner is blocking / attacking / parrying / staggered (full posture included). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|HUD")
	bool bShowWhileEngaged = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|HUD", meta = (ClampMin = "0.1"))
	float FadeOutSpeed = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|HUD", meta = (ClampMin = "0.1"))
	float FadeInSpeed = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|HUD")
	FLinearColor NormalFillColor = FLinearColor(0.85f, 0.66f, 0.24f, 1.f);

	/** Fill colour alternates between these two while posture is broken. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|HUD")
	FLinearColor BrokenFillColor = FLinearColor(0.72f, 0.12f, 0.08f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|HUD")
	FLinearColor FlashFillColor = FLinearColor(1.f, 0.93f, 0.75f, 1.f);

	/** Flashes per second while broken. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|HUD", meta = (ClampMin = "0.1"))
	float FlashRate = 4.f;

	/** Scale punch applied the moment posture breaks (1 = none). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|HUD", meta = (ClampMin = "1.0"))
	float BreakPunchScale = 1.25f;

protected:
	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|HUD", meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> Bar_Posture;

	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void HandlePostureChanged(float Posture, float MaxPosture) override;
	virtual void HandlePostureBrokenChanged(bool bBroken) override;

private:
	float DamageFraction = 0.f;
	bool bAtFull = true;
	float TimeAtFull = 0.f;
	float CurrentOpacity = 0.f;
	float FlashTime = 0.f;
	float PunchAlpha = 0.f;
};
