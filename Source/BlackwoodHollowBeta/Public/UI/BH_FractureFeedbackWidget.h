// Blackwood Hollow - Fracture local feedback widget (Phase 11G)
// Target: Unreal Engine 5.8 (C++), UMG
//
// One small reusable parent class; every instance watches the LOCAL player's pawn ASC for State.Status.Fracture (polled a few times a second:
// the pawn can be replaced on respawn, so nothing is cached) and shows the matching local-only feedback. Widget Blueprints bind by NAME,
// everything optional:
//
//   Img_Cracks         UImage  brush = M_UI_FractureCracks (UI-domain material). The widget makes it dynamic and eases the scalar
//                              CrackAmount between 0 (not fractured) and CrackIntensity (0.35 = faint). Collapsed at 0.
//   Img_FractureIcon   UImage  the status icon (the Fracture effect's own Icon, else FallbackIcon). Collapsed when not fractured.
//   Txt_FractureName   UTextBlock  the effect's DisplayName ("Fracture"). Collapsed when not fractured.
//
// Place instances where the feedback belongs (all share this class):
//   WBP_FractureCracks      Img_Cracks only, added as a fill overlay INSIDE WBP_VitalsCluster and again over the chest Heart-Fragment icon
//   WBP_FractureStatusIcon  Img_FractureIcon + Txt_FractureName, next to the Blight status in the HUD
// Purely cosmetic and local: no state, no replication.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BH_FractureFeedbackWidget.generated.h"

class UImage;
class UMaterialInstanceDynamic;
class UTextBlock;
class UTexture2D;

UCLASS(Abstract, Blueprintable)
class BLACKWOODHOLLOWBETA_API UBH_FractureFeedbackWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** CrackAmount reached while fractured (0..1). Faint by design. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|Fracture", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CrackIntensity = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|Fracture", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float CrackFadeInSeconds = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|Fracture", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float CrackFadeOutSeconds = 0.4f;

	/** Scalar parameter of M_UI_FractureCracks that drives the crack opacity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|Fracture")
	FName CrackParameterName = TEXT("CrackAmount");

	/** Seconds between checks of the local pawn's ASC. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|Fracture", meta = (ClampMin = "0.05", ForceUnits = "s"))
	float PollInterval = 0.2f;

	/** Icon shown when the active Fracture effect carries none (the GE_Fracture Blueprint child sets its own). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|Fracture")
	TObjectPtr<UTexture2D> FallbackIcon;

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Fracture")
	bool IsFractureActive() const { return bFractureActive; }

	/** Current eased crack amount, 0..CrackIntensity. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Fracture")
	float GetCrackAmount() const { return CrackAmount; }

	/** The local player became fractured / was repaired (also fires once on the first check with the current state). */
	UFUNCTION(BlueprintImplementableEvent, Category = "BlackwoodHollow|Fracture")
	void OnFractureChanged(bool bFractured);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|Fracture", meta = (BindWidgetOptional))
	TObjectPtr<UImage> Img_Cracks;

	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|Fracture", meta = (BindWidgetOptional))
	TObjectPtr<UImage> Img_FractureIcon;

	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|Fracture", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_FractureName;

private:
	void PollFracture();
	void SetFractureActive(bool bActive);
	void ApplyCrackAmount();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CrackMaterial;

	bool bFractureActive = false;
	bool bFirstPollDone = false;
	float CrackAmount = 0.f;
	float PollTimer = 0.f;
};
