// Blackwood Hollow - player settings (Phase 13H-1). See the header.

#include "Settings/BH_GameUserSettings.h"
#include "Combat/BH_CombatFeel.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"

DEFINE_LOG_CATEGORY_STATIC(LogBHSettings, Log, All);

namespace BHSettings
{
	static const TCHAR* DefaultMixPath = TEXT("/Game/BlackwoodHollow/Audio/Mix/SM_BH_Master.SM_BH_Master");
	static const TCHAR* DefaultMasterClassPath = TEXT("/Game/BlackwoodHollow/Audio/Mix/SC_BH_Master.SC_BH_Master");
	static const TCHAR* DefaultMusicClassPath = TEXT("/Game/BlackwoodHollow/Audio/Mix/SC_BH_Music.SC_BH_Music");
	static const TCHAR* DefaultSfxClassPath = TEXT("/Game/BlackwoodHollow/Audio/Mix/SC_BH_SFX.SC_BH_SFX");
	static const TCHAR* DefaultUiClassPath = TEXT("/Game/BlackwoodHollow/Audio/Mix/SC_BH_UI.SC_BH_UI");

	static float ClampSensitivity(float Value) { return FMath::Clamp(Value, 0.1f, 5.f); }

	static void SetShakeCVar(float Value)
	{
		if (IConsoleVariable* ShakeVar = IConsoleManager::Get().FindConsoleVariable(TEXT("bh.Combat.ShakeScale")))
		{
			ShakeVar->Set(Value, ECVF_SetByCode);
		}
	}
}

UBH_GameUserSettings::UBH_GameUserSettings()
{
	ResetAudioAssetsToDefaults();
}

UBH_GameUserSettings* UBH_GameUserSettings::Get()
{
	return GEngine ? Cast<UBH_GameUserSettings>(GEngine->GetGameUserSettings()) : nullptr;
}

void UBH_GameUserSettings::ResetAudioAssetsToDefaults()
{
	MasterSoundMix = TSoftObjectPtr<USoundMix>(FSoftObjectPath(BHSettings::DefaultMixPath));
	MasterSoundClass = TSoftObjectPtr<USoundClass>(FSoftObjectPath(BHSettings::DefaultMasterClassPath));
	MusicSoundClass = TSoftObjectPtr<USoundClass>(FSoftObjectPath(BHSettings::DefaultMusicClassPath));
	SfxSoundClass = TSoftObjectPtr<USoundClass>(FSoftObjectPath(BHSettings::DefaultSfxClassPath));
	UiSoundClass = TSoftObjectPtr<USoundClass>(FSoftObjectPath(BHSettings::DefaultUiClassPath));
}

void UBH_GameUserSettings::SetToDefaults()
{
	Super::SetToDefaults();

	MasterVolume = 1.f;
	MusicVolume = 1.f;
	SfxVolume = 1.f;
	UiVolume = 1.f;
	MouseSensitivityX = 1.f;
	MouseSensitivityY = 1.f;
	bInvertY = false;
	GamepadLookSensitivity = 1.f;
	bRumbleEnabled = true;
	CameraShakeScale = 1.f;
	HeartbeatMenuDuck = 0.4f;
	ResetAudioAssetsToDefaults();
}

// -- Setters --------------------------------------------------------------------------------

void UBH_GameUserSettings::SetMasterVolume(float NewVolume, const UObject* WorldContext)
{
	MasterVolume = FMath::Clamp(NewVolume, 0.f, 1.f);
	if (WorldContext) { ApplyNonVideoSettings(WorldContext); }
}

void UBH_GameUserSettings::SetMusicVolume(float NewVolume, const UObject* WorldContext)
{
	MusicVolume = FMath::Clamp(NewVolume, 0.f, 1.f);
	if (WorldContext) { ApplyNonVideoSettings(WorldContext); }
}

void UBH_GameUserSettings::SetSfxVolume(float NewVolume, const UObject* WorldContext)
{
	SfxVolume = FMath::Clamp(NewVolume, 0.f, 1.f);
	if (WorldContext) { ApplyNonVideoSettings(WorldContext); }
}

void UBH_GameUserSettings::SetUiVolume(float NewVolume, const UObject* WorldContext)
{
	UiVolume = FMath::Clamp(NewVolume, 0.f, 1.f);
	if (WorldContext) { ApplyNonVideoSettings(WorldContext); }
}

void UBH_GameUserSettings::SetMouseSensitivityX(float NewValue) { MouseSensitivityX = BHSettings::ClampSensitivity(NewValue); }
void UBH_GameUserSettings::SetMouseSensitivityY(float NewValue) { MouseSensitivityY = BHSettings::ClampSensitivity(NewValue); }
void UBH_GameUserSettings::SetInvertY(bool bNewInvert) { bInvertY = bNewInvert; }
void UBH_GameUserSettings::SetGamepadLookSensitivity(float NewValue) { GamepadLookSensitivity = BHSettings::ClampSensitivity(NewValue); }

void UBH_GameUserSettings::SetRumbleEnabled(bool bNewEnabled)
{
	bRumbleEnabled = bNewEnabled;
	UBH_CombatFeelLibrary::SetRumbleEnabled(bRumbleEnabled);
}

void UBH_GameUserSettings::SetCameraShakeScale(float NewValue)
{
	CameraShakeScale = FMath::Clamp(NewValue, 0.f, 1.f);
	BHSettings::SetShakeCVar(CameraShakeScale);
}

// -- Apply / save ---------------------------------------------------------------------------

void UBH_GameUserSettings::ApplyClassVolume(const UObject* WorldContext, USoundMix* Mix, USoundClass* SoundClass, float Volume, const TCHAR* Label) const
{
	if (!SoundClass)
	{
		static TSet<FString> LoggedMissing;
		bool bAlreadyLogged = false;
		LoggedMissing.Add(FString(Label), &bAlreadyLogged);
		if (!bAlreadyLogged)
		{
			UE_LOG(LogBHSettings, Warning, TEXT("Sound class '%s' is missing (created in 13H-2); its volume is not applied."), Label);
		}
		return;
	}
	UGameplayStatics::SetSoundMixClassOverride(WorldContext, Mix, SoundClass, Volume, 1.f, 0.f, true);
}

void UBH_GameUserSettings::ApplyNonVideoSettings(const UObject* WorldContext)
{
	// Rumble and shake scale need no world.
	UBH_CombatFeelLibrary::SetRumbleEnabled(bRumbleEnabled);
	BHSettings::SetShakeCVar(CameraShakeScale);

	if (!WorldContext)
	{
		return;
	}

	USoundMix* Mix = MasterSoundMix.LoadSynchronous();
	if (!Mix)
	{
		static bool bLoggedNoMix = false;
		if (!bLoggedNoMix)
		{
			bLoggedNoMix = true;
			UE_LOG(LogBHSettings, Warning, TEXT("Sound mix '%s' is missing (created in 13H-2); volumes are not applied."), *MasterSoundMix.ToString());
		}
		return;
	}

	ApplyClassVolume(WorldContext, Mix, MasterSoundClass.LoadSynchronous(), MasterVolume, TEXT("SC_BH_Master"));
	ApplyClassVolume(WorldContext, Mix, MusicSoundClass.LoadSynchronous(), MusicVolume, TEXT("SC_BH_Music"));
	ApplyClassVolume(WorldContext, Mix, SfxSoundClass.LoadSynchronous(), SfxVolume, TEXT("SC_BH_SFX"));
	ApplyClassVolume(WorldContext, Mix, UiSoundClass.LoadSynchronous(), UiVolume, TEXT("SC_BH_UI"));
	UGameplayStatics::PushSoundMixModifier(WorldContext, Mix);
}

void UBH_GameUserSettings::SaveBHSettings()
{
	SaveSettings();
}

void UBH_GameUserSettings::ResetNonVideoToDefaults(const UObject* WorldContext)
{
	MasterVolume = 1.f;
	MusicVolume = 1.f;
	SfxVolume = 1.f;
	UiVolume = 1.f;
	MouseSensitivityX = 1.f;
	MouseSensitivityY = 1.f;
	bInvertY = false;
	GamepadLookSensitivity = 1.f;
	bRumbleEnabled = true;
	CameraShakeScale = 1.f;
	ApplyNonVideoSettings(WorldContext);
}

// -- Video keep-or-revert -------------------------------------------------------------------

void UBH_GameUserSettings::BeginVideoPreview()
{
	VideoSnapshot.bValid = true;
	VideoSnapshot.QualityLevel = GetOverallScalabilityLevel();
	VideoSnapshot.Resolution = GetScreenResolution();
	VideoSnapshot.WindowMode = GetFullscreenMode();
	VideoSnapshot.bVSync = IsVSyncEnabled();
	VideoSnapshot.FrameRateLimit = GetFrameRateLimit();
}

void UBH_GameUserSettings::ConfirmVideoPreview()
{
	VideoSnapshot = FBH_VideoSnapshot();
	SaveSettings();
}

void UBH_GameUserSettings::RevertVideoPreview(bool bApplyNow)
{
	if (!VideoSnapshot.bValid)
	{
		return;
	}
	SetOverallScalabilityLevel(VideoSnapshot.QualityLevel);
	SetScreenResolution(VideoSnapshot.Resolution);
	SetFullscreenMode(VideoSnapshot.WindowMode);
	SetVSyncEnabled(VideoSnapshot.bVSync);
	SetFrameRateLimit(VideoSnapshot.FrameRateLimit);
	VideoSnapshot = FBH_VideoSnapshot();
	if (bApplyNow)
	{
		ApplySettings(false);
	}
}
