// Blackwood Hollow - interaction prompt base widget (Phase 11C)
// Target: Unreal Engine 5.8 (C++)
//
// Parent class for WBP_InteractPrompt. UBH_InteractorComponent creates one per local player (if the Blueprint exists), shows it while an
// interactable is focused and drives the hold progress. All named widgets are OPTIONAL, so the Blueprint only builds what it wants:
//   Txt_Name      TextBlock  "Supply Crate"
//   Txt_Action    TextBlock  "Open"  (the key glyph is a Blueprint decoration next to it)
//   Txt_Denied    TextBlock  greyed reason ("Already looted"), collapsed when empty
//   Pb_Hold       ProgressBar  hold progress 0..1, collapsed while idle
//   Img_HoldRing  Image with a material that has a scalar parameter "Progress" (optional radial ring)
// Everything is local and cosmetic; the server owns the actual hold.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "BH_InteractPromptWidget.generated.h"

class UImage;
class UMaterialInstanceDynamic;
class UProgressBar;
class UTextBlock;

UCLASS(Abstract, Blueprintable)
class BLACKWOODHOLLOWBETA_API UBH_InteractPromptWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Shows the prompt. bCanInteract=false greys it out and shows DenyReason. bHasHold shows the progress widgets. */
	UFUNCTION(BlueprintCallable, Category = "BH|Interaction")
	void SetPrompt(const FText& Name, const FText& Action, bool bCanInteract, const FText& DenyReason, bool bHasHold);

	/** 0..1 hold progress. */
	UFUNCTION(BlueprintCallable, Category = "BH|Interaction")
	void SetHoldProgress(float Fraction);

	UFUNCTION(BlueprintCallable, Category = "BH|Interaction")
	void HidePrompt();

	/** Name of the scalar parameter on Img_HoldRing's material. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Interaction")
	FName HoldRingProgressParameter = TEXT("Progress");

	/** Blueprint hook after SetPrompt (colour the action line, play an in-animation...). */
	UFUNCTION(BlueprintImplementableEvent, Category = "BH|Interaction")
	void BP_OnPromptShown(bool bCanInteract);

	UFUNCTION(BlueprintImplementableEvent, Category = "BH|Interaction")
	void BP_OnPromptHidden();

protected:
	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Name;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Action;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Denied;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> Pb_Hold;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> Img_HoldRing;

private:
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> HoldRingMID;

	bool bShowingHold = false;
};
