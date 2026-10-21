// Blackwood Hollow - Activatable menu widget base (Phase 13B)
// Target: Unreal Engine 5.8 (C++), CommonUI, EnhancedInput
//
// Base for full-screen menus (WBP_PlayerMenu). While active it:
//  * adds MenuMappingContext (IMC_Menu) to the owning local player's Enhanced Input subsystem at MenuMappingPriority (default 100,
//    above the combat context so Q / E tab navigation wins over Parry / Interact), and
//  * asks CommonUI for Menu input mode with no mouse capture (cursor visible, game input blocked).
// The context is removed in NativeOnDeactivated, and again (guarded) in NativeDestruct, so it can never get stuck.
// Local only; nothing is replicated.

#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "Input/UIActionBindingHandle.h"
#include "InputCoreTypes.h"
#include "BH_MenuActivatableWidget.generated.h"

class UInputMappingContext;

UCLASS(Abstract, Blueprintable)
class BLACKWOODHOLLOWBETA_API UBH_MenuActivatableWidget : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	/** Mapping context added while this menu is active (IMC_Menu: IA_UI_TabPrev / IA_UI_TabNext). May be null. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BlackwoodHollow|UI|Menu")
	TObjectPtr<UInputMappingContext> MenuMappingContext;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BlackwoodHollow|UI|Menu")
	int32 MenuMappingPriority = 100;

	UBH_MenuActivatableWidget(const FObjectInitializer& ObjectInitializer);

	/** Keys that close this menu while it is active (handled by an input pre-processor, so Slate focus navigation / CommonUI cannot swallow them). Esc is left to CommonUI Back. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BlackwoodHollow|UI|Menu")
	TArray<FKey> CloseKeys;

	/** 13G-0. Menus that show the once-per-session "Game not paused" caption set this (WBP_PlayerMenu). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BlackwoodHollow|UI|Menu")
	bool bShowNotPausedCue = false;

	/** True when this menu wants the cue AND it has not been shown yet this session. Call once from the menu's activation. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|UI|Menu")
	bool ConsumeNotPausedCue();

	virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override;

	/** Called by the close pre-processor. */
	bool TryCloseFromKey(const FKeyEvent& KeyEvent);

protected:
	virtual void NativeOnActivated() override;
	virtual void NativeOnDeactivated() override;
	virtual void NativeDestruct() override;

private:
	void AddMenuContext();
	void RemoveMenuContext();
	void RegisterCloseProcessor();
	void UnregisterCloseProcessor();

	/** Tell the layout subsystem a menu activated / deactivated so it can broadcast OnMenuStateChanged. */
	void NotifyLayoutMenuState() const;

	TSharedPtr<class IInputProcessor> CloseProcessor;

	/** True while the context is added by this widget (guards double add / remove). */
	bool bContextAdded = false;

	/** Subsystem owner of the added context, so it can still be removed if the player is already gone. */
	TWeakObjectPtr<class UEnhancedInputLocalPlayerSubsystem> ContextSubsystem;
};
