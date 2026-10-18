// Blackwood Hollow - C++ player controller base (Phase 13H-1)
// Target: Unreal Engine 5.8 (C++)
//
// New C++ parent for PC_BlackwoodHollow (reparent in Class Settings, 13H-2). Local-only behaviour:
//   - BeginPlay (local controller only): applies the saved settings (UBH_GameUserSettings::ApplyNonVideoSettings).
//   - AddYawInput / AddPitchInput: scales look by mouse sensitivity X/Y, or gamepad sensitivity when the last input was a gamepad,
//     and flips pitch when Invert Y is on. Every Add Controller Yaw/Pitch Input call goes through these.
//   - Heartbeat ducking: GetHeartbeatVolumeMultiplier() is HeartbeatMenuDuck (0.4) while this player's menu is open, else 1.
//     OnHeartbeatDuckChanged fires on every menu open/close (and once at start) so the heartbeat audio component can set its volume.
// Nothing here replicates.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "BH_PlayerController.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBH_OnHeartbeatDuckChanged, float, VolumeMultiplier);

UCLASS()
class BLACKWOODHOLLOWBETA_API ABH_PlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	virtual void AddYawInput(float Val) override;
	virtual void AddPitchInput(float Val) override;

	/** 1 normally; HeartbeatMenuDuck while this local player's menu is open. Multiply the low-health heartbeat volume by it. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Settings")
	float GetHeartbeatVolumeMultiplier() const;

	/** Fires with the new multiplier whenever the menu opens or closes. Bind in the Blueprint that owns the heartbeat audio. */
	UPROPERTY(BlueprintAssignable, Category = "BlackwoodHollow|Settings")
	FBH_OnHeartbeatDuckChanged OnHeartbeatDuckChanged;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void HandleMenuStateChanged(bool bAnyMenuOpen);

	bool IsGamepadActive() const;
};
