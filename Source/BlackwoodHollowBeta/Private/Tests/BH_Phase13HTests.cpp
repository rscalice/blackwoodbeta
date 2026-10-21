// Blackwood Hollow - Phase 13H-1 automation tests (settings defaults, SetToDefaults, video preview revert)
// Run: Session Frontend > Automation > "BlackwoodHollow.Phase13H". Uses a transient settings object: the engine's real settings are untouched.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Settings/BH_GameUserSettings.h"

#define BH_P13H_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_P13H_DefaultsTest, "BlackwoodHollow.Phase13H.Settings.Defaults", BH_P13H_FLAGS)
bool FBH_P13H_DefaultsTest::RunTest(const FString& Parameters)
{
	const UBH_GameUserSettings* Defaults = GetDefault<UBH_GameUserSettings>();
	TestEqual(TEXT("Master"), Defaults->MasterVolume, 1.f);
	TestEqual(TEXT("Music"), Defaults->MusicVolume, 1.f);
	TestEqual(TEXT("Sfx"), Defaults->SfxVolume, 1.f);
	TestEqual(TEXT("Ui"), Defaults->UiVolume, 1.f);
	TestEqual(TEXT("MouseX"), Defaults->MouseSensitivityX, 1.f);
	TestEqual(TEXT("MouseY"), Defaults->MouseSensitivityY, 1.f);
	TestFalse(TEXT("InvertY"), Defaults->bInvertY);
	TestEqual(TEXT("Gamepad"), Defaults->GamepadLookSensitivity, 1.f);
	TestTrue(TEXT("Rumble"), Defaults->bRumbleEnabled);
	TestEqual(TEXT("Shake"), Defaults->CameraShakeScale, 1.f);
	TestEqual(TEXT("HeartbeatDuck"), Defaults->HeartbeatMenuDuck, 0.4f);
	TestTrue(TEXT("Mix path"), Defaults->MasterSoundMix.ToString().EndsWith(TEXT("SM_BH_Master")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_P13H_SetToDefaultsTest, "BlackwoodHollow.Phase13H.Settings.SetToDefaults", BH_P13H_FLAGS)
bool FBH_P13H_SetToDefaultsTest::RunTest(const FString& Parameters)
{
	UBH_GameUserSettings* Settings = NewObject<UBH_GameUserSettings>(GetTransientPackage());
	Settings->MasterVolume = 0.2f;
	Settings->MusicVolume = 0.3f;
	Settings->SfxVolume = 0.4f;
	Settings->UiVolume = 0.5f;
	Settings->MouseSensitivityX = 3.f;
	Settings->MouseSensitivityY = 2.f;
	Settings->bInvertY = true;
	Settings->GamepadLookSensitivity = 4.f;
	Settings->bRumbleEnabled = false;
	Settings->CameraShakeScale = 0.f;
	Settings->SetToDefaults();
	TestEqual(TEXT("Master"), Settings->MasterVolume, 1.f);
	TestEqual(TEXT("Music"), Settings->MusicVolume, 1.f);
	TestEqual(TEXT("Sfx"), Settings->SfxVolume, 1.f);
	TestEqual(TEXT("Ui"), Settings->UiVolume, 1.f);
	TestEqual(TEXT("MouseX"), Settings->MouseSensitivityX, 1.f);
	TestEqual(TEXT("MouseY"), Settings->MouseSensitivityY, 1.f);
	TestFalse(TEXT("InvertY"), Settings->bInvertY);
	TestEqual(TEXT("Gamepad"), Settings->GamepadLookSensitivity, 1.f);
	TestTrue(TEXT("Rumble"), Settings->bRumbleEnabled);
	TestEqual(TEXT("Shake"), Settings->CameraShakeScale, 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_P13H_PreviewRevertTest, "BlackwoodHollow.Phase13H.Settings.PreviewRevert", BH_P13H_FLAGS)
bool FBH_P13H_PreviewRevertTest::RunTest(const FString& Parameters)
{
	UBH_GameUserSettings* Settings = NewObject<UBH_GameUserSettings>(GetTransientPackage());
	Settings->SetVSyncEnabled(false);
	Settings->SetFrameRateLimit(60.f);
	Settings->SetScreenResolution(FIntPoint(1280, 720));
	Settings->SetFullscreenMode(EWindowMode::Windowed);

	Settings->BeginVideoPreview();
	TestTrue(TEXT("Preview active"), Settings->IsVideoPreviewActive());

	Settings->SetVSyncEnabled(true);
	Settings->SetFrameRateLimit(144.f);
	Settings->SetScreenResolution(FIntPoint(1920, 1080));
	Settings->SetFullscreenMode(EWindowMode::WindowedFullscreen);

	Settings->RevertVideoPreview(false); // false: restore the values only, never touch the real viewport
	TestFalse(TEXT("Preview cleared"), Settings->IsVideoPreviewActive());
	TestFalse(TEXT("VSync restored"), Settings->IsVSyncEnabled());
	TestEqual(TEXT("Frame cap restored"), Settings->GetFrameRateLimit(), 60.f);
	TestEqual(TEXT("Resolution restored"), Settings->GetScreenResolution(), FIntPoint(1280, 720));
	TestEqual(TEXT("Window mode restored"), static_cast<int32>(Settings->GetFullscreenMode()), static_cast<int32>(EWindowMode::Windowed));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
