// Blackwood Hollow - player settings (Phase 13H-1)
// Target: Unreal Engine 5.8 (C++)
//
// UBH_GameUserSettings extends UGameUserSettings (registered with GameUserSettingsClassName in DefaultEngine.ini), so everything
// is saved in Saved/Config/<Platform>/GameUserSettings.ini. Settings are LOCAL per machine: nothing here replicates.
//
// Video uses the built-in UGameUserSettings fields (overall quality, resolution, window mode, VSync, frame cap).
// Audio / controls / accessibility are the custom fields below, pushed by ApplyNonVideoSettings:
//   Volumes ....... soft sound mix SM_BH_Master + four sound classes (SC_BH_Master / Music / SFX / UI): one class override each,
//                   then the mix is pushed. The assets are authored in 13H-2; while one is missing the code logs once and skips it.
//   Rumble ........ UBH_CombatFeelLibrary::SetRumbleEnabled.
//   Shake scale ... CVar bh.Combat.ShakeScale.
// Look sensitivity / invert Y / gamepad sensitivity are read by ABH_PlayerController::AddYawInput / AddPitchInput.
//
// KEEP-OR-REVERT (video): BeginVideoPreview() snapshots, the widget applies the new video settings and runs its own 10 s timer, then
// ConfirmVideoPreview() keeps them (saves) or RevertVideoPreview() restores the snapshot.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"
#include "Engine/EngineTypes.h"
#include "BH_GameUserSettings.generated.h"

class USoundMix;
class USoundClass;

/** Video values captured by BeginVideoPreview. */
USTRUCT(BlueprintType)
struct BLACKWOODHOLLOWBETA_API FBH_VideoSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Video")
	bool bValid = false;

	UPROPERTY(BlueprintReadOnly, Category = "Video")
	int32 QualityLevel = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Video")
	FIntPoint Resolution = FIntPoint::ZeroValue;

	UPROPERTY(BlueprintReadOnly, Category = "Video")
	TEnumAsByte<EWindowMode::Type> WindowMode = EWindowMode::Windowed;

	UPROPERTY(BlueprintReadOnly, Category = "Video")
	bool bVSync = false;

	UPROPERTY(BlueprintReadOnly, Category = "Video")
	float FrameRateLimit = 0.f;
};

UCLASS(config = GameUserSettings, BlueprintType)
class BLACKWOODHOLLOWBETA_API UBH_GameUserSettings : public UGameUserSettings
{
	GENERATED_BODY()

public:
	UBH_GameUserSettings();

	/** The active settings object (null only if the engine's class is not this one, which means the ini registration is missing). */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Settings")
	static UBH_GameUserSettings* Get();

	virtual void SetToDefaults() override;

	// -- Audio ---------------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Settings|Audio") float GetMasterVolume() const { return MasterVolume; }
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Settings|Audio") float GetMusicVolume() const { return MusicVolume; }
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Settings|Audio") float GetSfxVolume() const { return SfxVolume; }
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Settings|Audio") float GetUiVolume() const { return UiVolume; }

	/** Setters clamp to 0..1, store, and (when WorldContext is valid) re-apply the non-video settings immediately. Call SaveBHSettings to persist. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Settings|Audio", meta = (WorldContext = "WorldContext"))
	void SetMasterVolume(float NewVolume, const UObject* WorldContext = nullptr);
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Settings|Audio", meta = (WorldContext = "WorldContext"))
	void SetMusicVolume(float NewVolume, const UObject* WorldContext = nullptr);
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Settings|Audio", meta = (WorldContext = "WorldContext"))
	void SetSfxVolume(float NewVolume, const UObject* WorldContext = nullptr);
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Settings|Audio", meta = (WorldContext = "WorldContext"))
	void SetUiVolume(float NewVolume, const UObject* WorldContext = nullptr);

	// -- Controls ------------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Settings|Controls") float GetMouseSensitivityX() const { return MouseSensitivityX; }
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Settings|Controls") float GetMouseSensitivityY() const { return MouseSensitivityY; }
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Settings|Controls") bool GetInvertY() const { return bInvertY; }
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Settings|Controls") float GetGamepadLookSensitivity() const { return GamepadLookSensitivity; }
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Settings|Controls") bool GetRumbleEnabled() const { return bRumbleEnabled; }

	/** Sensitivities clamp to 0.1..5. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Settings|Controls") void SetMouseSensitivityX(float NewValue);
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Settings|Controls") void SetMouseSensitivityY(float NewValue);
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Settings|Controls") void SetInvertY(bool bNewInvert);
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Settings|Controls") void SetGamepadLookSensitivity(float NewValue);
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Settings|Controls") void SetRumbleEnabled(bool bNewEnabled);

	// -- Accessibility -------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Settings|Accessibility") float GetCameraShakeScale() const { return CameraShakeScale; }
	/** Clamps to 0..1. Applies the bh.Combat.ShakeScale CVar immediately. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Settings|Accessibility") void SetCameraShakeScale(float NewValue);

	// -- Heartbeat -----------------------------------------------------------------------------

	/** Multiplier on the low-health heartbeat volume while the local player's menu is open. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Settings|Audio") float GetHeartbeatMenuDuck() const { return HeartbeatMenuDuck; }

	// -- Apply / save --------------------------------------------------------------------------

	/** Pushes volumes (sound mix), rumble and shake scale. Safe with a null context (audio is skipped). Local only. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Settings", meta = (WorldContext = "WorldContext"))
	void ApplyNonVideoSettings(const UObject* WorldContext);

	/** SaveSettings() (writes GameUserSettings.ini). */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Settings")
	void SaveBHSettings();

	/** Resets only the custom (non-video) fields to their defaults and applies them. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Settings", meta = (WorldContext = "WorldContext"))
	void ResetNonVideoToDefaults(const UObject* WorldContext);

	// -- Video keep-or-revert ------------------------------------------------------------------

	/** Snapshots the current video settings. Call before applying a change the player must confirm. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Settings|Video")
	void BeginVideoPreview();

	/** Keeps the previewed video settings: drops the snapshot and saves. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Settings|Video")
	void ConfirmVideoPreview();

	/** Restores the snapshot (no-op without one). bApplyNow applies it to the engine; false only restores the stored values (tests). */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Settings|Video")
	void RevertVideoPreview(bool bApplyNow = true);

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Settings|Video")
	bool IsVideoPreviewActive() const { return VideoSnapshot.bValid; }

	// -- Fields (config, saved in GameUserSettings.ini) ----------------------------------------

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Audio", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MasterVolume = 1.f;
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Audio", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MusicVolume = 1.f;
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Audio", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SfxVolume = 1.f;
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Audio", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float UiVolume = 1.f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Controls", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float MouseSensitivityX = 1.f;
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Controls", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float MouseSensitivityY = 1.f;
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Controls")
	bool bInvertY = false;
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Controls", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float GamepadLookSensitivity = 1.f;
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Controls")
	bool bRumbleEnabled = true;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Accessibility", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CameraShakeScale = 1.f;

	/** Heartbeat volume multiplier while the local player's menu is open (Blight creep / Rot vignette are not ducked). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Audio", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HeartbeatMenuDuck = 0.4f;

	// -- Sound assets (authored in 13H-2) ------------------------------------------------------

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Audio|Assets")
	TSoftObjectPtr<USoundMix> MasterSoundMix;
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Audio|Assets")
	TSoftObjectPtr<USoundClass> MasterSoundClass;
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Audio|Assets")
	TSoftObjectPtr<USoundClass> MusicSoundClass;
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Audio|Assets")
	TSoftObjectPtr<USoundClass> SfxSoundClass;
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Audio|Assets")
	TSoftObjectPtr<USoundClass> UiSoundClass;

private:
	/** Default asset paths (also what SetToDefaults restores). */
	void ResetAudioAssetsToDefaults();
	/** Applies one class override; logs once when SoundClass is null. */
	void ApplyClassVolume(const UObject* WorldContext, USoundMix* Mix, USoundClass* SoundClass, float Volume, const TCHAR* Label) const;

	FBH_VideoSnapshot VideoSnapshot;
};
