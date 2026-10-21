// Blackwood Hollow - Per-local-player UI layout owner (Phase 13B)
// Target: Unreal Engine 5.8 (C++), UMG, CommonUI
//
// Creates and owns the UBH_PrimaryLayoutWidget (WBP_PrimaryLayout) for one local player and is the single entry point for
// pushing menus onto its layers. Local only; nothing here is replicated, so it behaves the same for the host and for clients.
//
// Setup: the player controller (locally controlled only) calls EnsureLayout(WBP_PrimaryLayout class) once, e.g. from BeginPlay or
// Received Restart. It is idempotent: repeated calls return the existing layout. The layout is added to the player's screen
// at z-order LayoutZOrder (above the legacy HUD, which UBH_HUDSubsystem still adds at 0/1 until the HUD is rehomed).

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "GameplayTagContainer.h"
#include "BH_UILayoutSubsystem.generated.h"

class UBH_PrimaryLayoutWidget;
class UCommonActivatableWidget;
class UUserWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBH_OnMenuStateChanged, bool, bAnyMenuOpen);

UCLASS(BlueprintType)
class BLACKWOODHOLLOWBETA_API UBH_UILayoutSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	/** Creates the layout on first use and adds it to this player's screen. Returns null if there is no player controller yet. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|UI|Layout")
	UBH_PrimaryLayoutWidget* EnsureLayout(TSubclassOf<UBH_PrimaryLayoutWidget> LayoutClass);

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|UI|Layout")
	UBH_PrimaryLayoutWidget* GetLayout() const { return Layout; }

	/** Pushes a widget onto UI.Layer.GameMenu / Menu / Modal. Null (with a warning) if EnsureLayout has not run. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|UI|Layout")
	UCommonActivatableWidget* PushWidgetToLayer(FGameplayTag LayerTag, TSubclassOf<UCommonActivatableWidget> WidgetClass);

	/** Adds a non-activatable HUD widget to the Game layer. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|UI|Layout")
	void AddHUDWidget(UUserWidget* HUDWidget);

	/** True when any menu / popup layer has an active widget (false when there is no layout). */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|UI|Layout")
	bool IsAnyMenuOpen() const;

	/** Native event: the layout widget was just created and added to the screen (the HUD subsystem moves the HUD into the Game layer). */
	FSimpleMulticastDelegate OnLayoutReady;

	/** Fires when the open/closed state of the menus changes (true = at least one menu open). Local per player; HUDs bind instead of polling. */
	UPROPERTY(BlueprintAssignable, Category = "BlackwoodHollow|UI|Layout")
	FBH_OnMenuStateChanged OnMenuStateChanged;

	/** Called by menus on activate / deactivate and by the layout when a stack settles: re-evaluates IsAnyMenuOpen, broadcasts OnMenuStateChanged on a change, and (re)applies game input whenever no menu is open. */
	void NotifyMenuStateChanged();

	/** When bAnyMenuOpen is false: put CommonUI's router, the cursor and Slate focus back to game input now and again next tick. No-op while a menu is open. */
	void ApplyInputForMenuState(bool bAnyMenuOpen);

	/** Index of the player menu tab that was showing when it last closed, so it reopens there. Session only (not saved). */
	UPROPERTY(BlueprintReadWrite, Category = "BlackwoodHollow|UI|Layout")
	int32 LastPlayerMenuTab = 0;

	/** Screen z-order of the layout. Well above the legacy HUD (z 0/1), so a menu always draws over the HUD. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|UI|Layout")
	int32 LayoutZOrder = 100;

	/** 13G-0. Returns true exactly once per session (per local player): the first call hands out the "Game not paused" cue, every later call returns false. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|UI|Layout")
	bool ConsumeFirstOpenCue()
	{
		if (bFirstOpenCueConsumed)
		{
			return false;
		}
		bFirstOpenCueConsumed = true;
		return true;
	}

	virtual void Deinitialize() override;

private:
	bool bFirstOpenCueConsumed = false;

	UPROPERTY(Transient)
	TObjectPtr<UBH_PrimaryLayoutWidget> Layout;

	bool bLastAnyMenuOpen = false;

	void RestoreGameInputNow() const;
};
