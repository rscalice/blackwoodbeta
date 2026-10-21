// Blackwood Hollow - UI helper function library (Phase 13B, implementation)

#include "UI/BH_UIFunctionLibrary.h"
#include "Components/Image.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/PlayerController.h"
#include "Engine/LocalPlayer.h"
#include "InputAction.h"
#include "PlayerMappableKeySettings.h"
#include "EnhancedInputSubsystems.h"
#include "UserSettings/EnhancedInputUserSettings.h"

const FName UBH_UIFunctionLibrary::WidgetWidthParamName(TEXT("WidgetWidthPx"));
const FName UBH_UIFunctionLibrary::WidgetHeightParamName(TEXT("WidgetHeightPx"));

namespace BH_UI
{
	/** True when Key belongs to the requested device class. */
	static bool KeyMatchesDevice(const FKey& Key, ECommonInputType InputType)
	{
		if (!Key.IsValid())
		{
			return false;
		}
		switch (InputType)
		{
		case ECommonInputType::Gamepad:
			return Key.IsGamepadKey();
		case ECommonInputType::MouseAndKeyboard:
			return !Key.IsGamepadKey() && !Key.IsTouch();
		default:
			return false;
		}
	}
}

void UBH_UIFunctionLibrary::SetWidgetPixelSize(UMaterialInstanceDynamic* MID, FVector2D SizePx)
{
	if (!MID)
	{
		return;
	}
	MID->SetScalarParameterValue(WidgetWidthParamName, static_cast<float>(SizePx.X));
	MID->SetScalarParameterValue(WidgetHeightParamName, static_cast<float>(SizePx.Y));
}

UMaterialInstanceDynamic* UBH_UIFunctionLibrary::SetImagePixelSize(UImage* Image, FVector2D SizePx)
{
	if (!Image)
	{
		return nullptr;
	}

	if (SizePx.X <= 0.0 && SizePx.Y <= 0.0)
	{
		// Not laid out yet on the first Pre Construct, so fall back to the desired size (brush size / explicit size override).
		SizePx = FVector2D(Image->GetCachedGeometry().GetLocalSize());
		if (SizePx.X <= 0.0 && SizePx.Y <= 0.0)
		{
			SizePx = Image->GetDesiredSize();
		}
		if (SizePx.X <= 0.0 && SizePx.Y <= 0.0)
		{
			return nullptr;
		}
	}

	// GetDynamicMaterial returns the existing MID or creates one when the brush resource is a material; null otherwise.
	UMaterialInstanceDynamic* MID = Image->GetDynamicMaterial();
	SetWidgetPixelSize(MID, SizePx);
	return MID;
}

bool UBH_UIFunctionLibrary::GetKeyForInputAction(const APlayerController* PC, const UInputAction* Action, ECommonInputType InputType, FKey& OutKey)
{
	OutKey = FKey();

	if (!PC || !Action || InputType == ECommonInputType::Touch)
	{
		return false;
	}
	const ULocalPlayer* LocalPlayer = PC->GetLocalPlayer();
	if (!LocalPlayer)
	{
		return false;
	}
	const UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	if (!Subsystem)
	{
		return false;
	}

	// 1) Player-mappable path: respects rebinds saved in the active key profile.
	if (const UPlayerMappableKeySettings* KeySettings = Action->GetPlayerMappableKeySettings())
	{
		if (const UEnhancedInputUserSettings* UserSettings = Subsystem->GetUserSettings())
		{
			if (const UEnhancedPlayerMappableKeyProfile* Profile = UserSettings->GetActiveKeyProfile())
			{
				if (const FKeyMappingRow* Row = Profile->GetPlayerMappingRows().Find(KeySettings->Name))
				{
					for (const FPlayerKeyMapping& Mapping : Row->Mappings)
					{
						const FKey& Key = Mapping.GetCurrentKey();
						if (BH_UI::KeyMatchesDevice(Key, InputType))
						{
							OutKey = Key;
							return true;
						}
					}
				}
			}
		}
	}

	// 2) Fallback: whatever the applied mapping contexts currently map to this action.
	const TArray<FKey> Keys = Subsystem->QueryKeysMappedToAction(Action);
	for (const FKey& Key : Keys)
	{
		if (BH_UI::KeyMatchesDevice(Key, InputType))
		{
			OutKey = Key;
			return true;
		}
	}
	return false;
}
