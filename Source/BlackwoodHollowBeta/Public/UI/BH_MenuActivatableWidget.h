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

	virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override;

protected:
	virtual void NativeOnActivated() override;
	virtual void NativeOnDeactivated() override;
	virtual void NativeDestruct() override;

private:
	void AddMenuContext();
	void RemoveMenuContext();

	/** True while the context is added by this widget (guards double add / remove). */
	bool bContextAdded = false;

	/** Subsystem owner of the added context, so it can still be removed if the player is already gone. */
	TWeakObjectPtr<class UEnhancedInputLocalPlayerSubsystem> ContextSubsystem;
};
