// Blackwood Hollow - UI helper function library (Phase 13B)
// Target: Unreal Engine 5.8 (C++), UMG, CommonInput, EnhancedInput
//
// Two small helpers every UI Blueprint needs:
//  * SetWidgetPixelSize  - feeds the WidgetWidthPx / WidgetHeightPx scalar parameters used by the pixel-correct UI materials
//                          (M_UI_Frame and its instances, the slanted vitals bar) so borders and corners stay a fixed pixel size.
//  * GetKeyForInputAction - resolves which key is currently bound to an Enhanced Input action for a device type, so
//                          WBP_UI_InputGlyph can show the real (and rebound) key instead of a hand-typed key name.
// UI is local-only per player; nothing here is replicated.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "InputCoreTypes.h"
#include "CommonInputTypeEnum.h"
#include "BH_UIFunctionLibrary.generated.h"

class UMaterialInstanceDynamic;
class UImage;
class UInputAction;
class APlayerController;

UCLASS()
class BLACKWOODHOLLOWBETA_API UBH_UIFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Scalar parameter names written by SetWidgetPixelSize (kept in one place so materials and code agree). */
	static const FName WidgetWidthParamName;
	static const FName WidgetHeightParamName;

	/**
	 * Writes WidgetWidthPx / WidgetHeightPx on a dynamic material instance. Null-safe (does nothing for a null MID).
	 * Call from a widget's Pre Construct / On Size Changed with the widget's local size.
	 */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "BlackwoodHollow|UI")
	static void SetWidgetPixelSize(UMaterialInstanceDynamic* MID, FVector2D SizePx);

	/**
	 * Same, for an Image widget: gets (or creates) the image's dynamic material and writes the size.
	 * If SizePx is zero on both axes the image's cached geometry local size is used, then its desired size.
	 * Returns the dynamic material that was written (null if the image has no material brush or no usable size yet).
	 */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "BlackwoodHollow|UI")
	static UMaterialInstanceDynamic* SetImagePixelSize(UImage* Image, FVector2D SizePx = FVector2D::ZeroVector);

	/**
	 * Finds the key currently bound to Action for the given device type.
	 * 1) Enhanced Input user settings: when the action has Player Mappable Key Settings, the active key profile is queried so
	 *    player rebinds are respected.
	 * 2) Fallback: the player's currently applied mapping contexts (QueryKeysMappedToAction), filtered by device type.
	 * InputType: Gamepad -> gamepad keys only; MouseAndKeyboard -> non-gamepad keys; Touch -> always false.
	 * Returns false (and an invalid OutKey) when nothing is bound for that device.
	 */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "BlackwoodHollow|UI")
	static bool GetKeyForInputAction(const APlayerController* PC, const UInputAction* Action, ECommonInputType InputType, FKey& OutKey);
};
