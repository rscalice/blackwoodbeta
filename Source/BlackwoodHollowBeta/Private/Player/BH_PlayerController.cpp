// Blackwood Hollow - C++ player controller base (Phase 13H-1). See the header.

#include "Player/BH_PlayerController.h"
#include "CommonInputSubsystem.h"
#include "CommonInputTypeEnum.h"
#include "Engine/LocalPlayer.h"
#include "Settings/BH_GameUserSettings.h"
#include "UI/BH_UILayoutSubsystem.h"

bool ABH_PlayerController::IsGamepadActive() const
{
	const ULocalPlayer* LocalPlr = GetLocalPlayer();
	const UCommonInputSubsystem* InputSubsystem = LocalPlr ? UCommonInputSubsystem::Get(LocalPlr) : nullptr;
	return InputSubsystem && InputSubsystem->GetCurrentInputType() == ECommonInputType::Gamepad;
}

void ABH_PlayerController::AddYawInput(float Val)
{
	if (const UBH_GameUserSettings* BHSettings = IsLocalController() ? UBH_GameUserSettings::Get() : nullptr)
	{
		Val *= IsGamepadActive() ? BHSettings->GamepadLookSensitivity : BHSettings->MouseSensitivityX;
	}
	Super::AddYawInput(Val);
}

void ABH_PlayerController::AddPitchInput(float Val)
{
	if (const UBH_GameUserSettings* BHSettings = IsLocalController() ? UBH_GameUserSettings::Get() : nullptr)
	{
		Val *= IsGamepadActive() ? BHSettings->GamepadLookSensitivity : BHSettings->MouseSensitivityY;
		if (BHSettings->bInvertY)
		{
			Val = -Val;
		}
	}
	Super::AddPitchInput(Val);
}

float ABH_PlayerController::GetHeartbeatVolumeMultiplier() const
{
	const ULocalPlayer* LocalPlr = GetLocalPlayer();
	const UBH_UILayoutSubsystem* Layout = LocalPlr ? LocalPlr->GetSubsystem<UBH_UILayoutSubsystem>() : nullptr;
	if (Layout && Layout->IsAnyMenuOpen())
	{
		const UBH_GameUserSettings* BHSettings = UBH_GameUserSettings::Get();
		return BHSettings ? BHSettings->HeartbeatMenuDuck : 0.4f;
	}
	return 1.f;
}

void ABH_PlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController())
	{
		return;
	}

	if (UBH_GameUserSettings* BHSettings = UBH_GameUserSettings::Get())
	{
		BHSettings->ApplyNonVideoSettings(this);
	}

	if (const ULocalPlayer* LocalPlr = GetLocalPlayer())
	{
		if (UBH_UILayoutSubsystem* Layout = LocalPlr->GetSubsystem<UBH_UILayoutSubsystem>())
		{
			Layout->OnMenuStateChanged.AddUniqueDynamic(this, &ABH_PlayerController::HandleMenuStateChanged);
		}
	}
	OnHeartbeatDuckChanged.Broadcast(GetHeartbeatVolumeMultiplier());
}

void ABH_PlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (const ULocalPlayer* LocalPlr = GetLocalPlayer())
	{
		if (UBH_UILayoutSubsystem* Layout = LocalPlr->GetSubsystem<UBH_UILayoutSubsystem>())
		{
			Layout->OnMenuStateChanged.RemoveDynamic(this, &ABH_PlayerController::HandleMenuStateChanged);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void ABH_PlayerController::HandleMenuStateChanged(bool bAnyMenuOpen)
{
	OnHeartbeatDuckChanged.Broadcast(GetHeartbeatVolumeMultiplier());
}
