// Blackwood Hollow - Primary UI layout (Phase 13B)
// Target: Unreal Engine 5.8 (C++), UMG, CommonUI
//
// One instance per local player, created and owned by UBH_UILayoutSubsystem. A Blueprint child (WBP_PrimaryLayout) supplies the
// four named layers by name (BindWidget), stacked in this order (back to front):
//
//   UI.Layer.Game      GameLayer      UOverlay*  Plain, NON-activatable layer for the always-on HUD (WBP_HUD_Main, boss bar, ...).
//                                     It is an overlay rather than an activatable stack because the HUD is never "activated" or
//                                     closed and must not take part in CommonUI input routing. Use AddHUDWidget.
//   UI.Layer.GameMenu  GameMenuStack  Activatable stack for in-game overlays that sit above the HUD (radial menus, loot windows).
//   UI.Layer.Menu      MenuStack      Activatable stack for full menus (the player menu: Inventory / Map / Quests).
//   UI.Layer.Modal     ModalStack     Activatable stack for confirmations and popups; always on top.
//
// PushWidgetToLayer only works for the three activatable layers; pushing to UI.Layer.Game logs a warning and returns null
// (use AddHUDWidget instead). UI is local-only per player; nothing here is replicated.

#pragma once

#include "CoreMinimal.h"
#include "CommonUserWidget.h"
#include "GameplayTagContainer.h"
#include "BH_PrimaryLayoutWidget.generated.h"

class UOverlay;
class UCommonActivatableWidget;
class UCommonActivatableWidgetStack;
class UCommonActivatableWidgetContainerBase;

UCLASS(Abstract, Blueprintable)
class BLACKWOODHOLLOWBETA_API UBH_PrimaryLayoutWidget : public UCommonUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * Creates Class on the layer named by LayerTag (UI.Layer.GameMenu / Menu / Modal) and returns it, or null on failure
	 * (unknown tag, the non-activatable Game layer, null class, or the layer widget is not bound).
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|UI|Layout", meta = (DeterminesOutputType = "WidgetClass"))
	UCommonActivatableWidget* PushWidgetToLayer(FGameplayTag LayerTag, TSubclassOf<UCommonActivatableWidget> WidgetClass);

	/** Adds an already created, non-activatable widget (the HUD) to the Game layer, filling it. Null-safe; ignores repeats. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|UI|Layout")
	void AddHUDWidget(UUserWidget* HUDWidget);

	/** Removes a widget previously added with AddHUDWidget from the Game layer. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|UI|Layout")
	void RemoveHUDWidget(UUserWidget* HUDWidget);

	/** The activatable stack for a layer tag, or null for UI.Layer.Game / unknown tags. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|UI|Layout")
	UCommonActivatableWidgetStack* GetLayerStack(FGameplayTag LayerTag) const;

	/** True when any of the GameMenu / Menu / Modal layers currently has an active widget. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|UI|Layout")
	bool IsAnyMenuOpen() const;

protected:
	/** Non-activatable HUD layer (UI.Layer.Game). */
	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|UI|Layout", meta = (BindWidget))
	TObjectPtr<UOverlay> GameLayer;

	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|UI|Layout", meta = (BindWidget))
	TObjectPtr<UCommonActivatableWidgetStack> GameMenuStack;

	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|UI|Layout", meta = (BindWidget))
	TObjectPtr<UCommonActivatableWidgetStack> MenuStack;

	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|UI|Layout", meta = (BindWidget))
	TObjectPtr<UCommonActivatableWidgetStack> ModalStack;
};
