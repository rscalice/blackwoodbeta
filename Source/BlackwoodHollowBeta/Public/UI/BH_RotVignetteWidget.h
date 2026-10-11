// Blackwood Hollow - full-screen Blight Rot vignette (Phase 11B)
// Target: Unreal Engine 5.8 (C++), UMG
//
// Parent of WBP_RotVignette. UBH_BlightStatusWidget creates one for the LOCAL player the first time a Blight Rot needs it
// (UBH_BlightStatusWidget::RotVignetteWidgetClass), adds it to the player's screen BEHIND the HUD and feeds it the eased Rot intensity (0..1).
// Nothing here replicates: it is a purely local cosmetic.
//
// Widget Blueprint contract (all optional):
//   Img_Vignette  UImage  full-screen image. Give it a UI-domain material with a scalar parameter named IntensityParameterName
//                         ("Intensity" by default); the widget makes a dynamic instance of it and sets the parameter. A plain texture
//                         also works: turn on bDriveRenderOpacity and the image fades by render opacity instead.
// The widget collapses itself at intensity 0 and never takes mouse input.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BH_RotVignetteWidget.generated.h"

class UImage;
class UMaterialInstanceDynamic;

UCLASS(Abstract, Blueprintable)
class BLACKWOODHOLLOWBETA_API UBH_RotVignetteWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Scalar parameter of Img_Vignette's material that receives the intensity (0 = invisible, 1 = full vignette). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|Blight")
	FName IntensityParameterName = TEXT("Intensity");

	/** Also fade Img_Vignette by render opacity (for a plain texture with no material parameter). Leave off when the material already uses the parameter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|Blight")
	bool bDriveRenderOpacity = false;

	/** Sets the vignette strength (clamped to 0..1) and shows / collapses the widget. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Blight")
	void SetIntensity(float NewIntensity);

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Blight")
	float GetIntensity() const { return Intensity; }

	/** The intensity changed (after the material was updated): hook extra effects here (a heartbeat, a pulse animation). */
	UFUNCTION(BlueprintImplementableEvent, Category = "BlackwoodHollow|Blight")
	void OnIntensityChanged(float NewIntensity);

protected:
	virtual void NativeConstruct() override;

	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|Blight", meta = (BindWidgetOptional))
	TObjectPtr<UImage> Img_Vignette;

private:
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> VignetteMaterial;

	float Intensity = 0.f;
};
