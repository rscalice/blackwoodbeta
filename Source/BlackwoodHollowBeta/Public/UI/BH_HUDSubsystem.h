// Blackwood Hollow - Per-local-player HUD owner
// Target: Unreal Engine 5.8 (C++), UMG
//
// Owns the production HUD (a UBH_HUDWidget, e.g. WBP_HUD_Main) and the debug
// HUD (any UUserWidget, e.g. W_BH_DebugHUD) for one local player, and switches
// between them. Only one is visible at a time.
//
// Toggle from the console:  BH.HUD.ToggleDebug   or   BH.HUD.Debug 0|1
// or from Blueprint: UBH_CombatFunctionLibrary::ToggleDebugHUD.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "BH_HUDSubsystem.generated.h"

class APawn;
class UUserWidget;
class UBH_HUDWidget;

UCLASS()
class BLACKWOODHOLLOWBETA_API UBH_HUDSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * Creates the HUDs on first use (reused afterwards) and binds the production
	 * HUD to Pawn's AbilitySystemComponent. Pawn must be controlled by this local player.
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|HUD")
	bool SetupHUD(APawn* Pawn, TSubclassOf<UBH_HUDWidget> MainHUDClass, TSubclassOf<UUserWidget> DebugHUDClass);

	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|HUD")
	void SetDebugHUDVisible(bool bVisible);

	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|HUD")
	void ToggleDebugHUD() { SetDebugHUDVisible(!bDebugVisible); }

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|HUD")
	bool IsDebugHUDVisible() const { return bDebugVisible; }

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|HUD")
	UBH_HUDWidget* GetMainHUD() const { return MainHUD; }

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|HUD")
	UUserWidget* GetDebugHUD() const { return DebugHUD; }

	virtual void Deinitialize() override;

private:
	void ApplyVisibility();

	UPROPERTY(Transient)
	TObjectPtr<UBH_HUDWidget> MainHUD;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> DebugHUD;

	bool bDebugVisible = false;
};
