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

UCLASS()
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

	/** Fires when the open/closed state of the menus changes (true = at least one menu open). Local per player; HUDs bind instead of polling. */
	UPROPERTY(BlueprintAssignable, Category = "BlackwoodHollow|UI|Layout")
	FBH_OnMenuStateChanged OnMenuStateChanged;

	/** Called by menus on activate / deactivate: re-evaluates IsAnyMenuOpen and broadcasts OnMenuStateChanged if it changed. */
	void NotifyMenuStateChanged();

	/** Index of the player menu tab that was showing when it last closed, so it reopens there. Session only (not saved). */
	UPROPERTY(BlueprintReadWrite, Category = "BlackwoodHollow|UI|Layout")
	int32 LastPlayerMenuTab = 0;

	/** Screen z-order of the layout. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|UI|Layout")
	int32 LayoutZOrder = 10;

	virtual void Deinitialize() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UBH_PrimaryLayoutWidget> Layout;

	bool bLastAnyMenuOpen = false;
};
