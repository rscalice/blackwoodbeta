// Blackwood Hollow - default item icons (issue #122)
// Target: Unreal Engine 5.8 (C++)
//
// Project Settings > Game > "Blackwood Hollow Item Icons" (saved to DefaultGame.ini). One placeholder texture per item category, used by
// UBH_PlayerMenuLibrary::GetItemIcon / GetItemIconDefault for items that have no Narrative Thumbnail. Assign the four textures once here
// instead of on every Blueprint call site (WBP_Inv_EquipSlot.ShowItem, WBP_Inv_BagItem.Refresh, WBP_Inv_ItemDetail.FillHeader).

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "UObject/SoftObjectPtr.h"
#include "BH_ItemIconSettings.generated.h"

class UTexture2D;

UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Blackwood Hollow Item Icons"))
class BLACKWOODHOLLOWBETA_API UBH_ItemIconSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UBH_ItemIconSettings()
	{
		CategoryName = TEXT("Game");
	}

	static const UBH_ItemIconSettings* Get() { return GetDefault<UBH_ItemIconSettings>(); }

	/** Fallback icon for weapons without a Thumbnail. */
	UPROPERTY(Config, EditAnywhere, Category = "Fallback Icons", meta = (AllowedClasses = "/Script/Engine.Texture2D"))
	TSoftObjectPtr<UTexture2D> WeaponFallback;

	/** Fallback icon for armor pieces without a Thumbnail. */
	UPROPERTY(Config, EditAnywhere, Category = "Fallback Icons", meta = (AllowedClasses = "/Script/Engine.Texture2D"))
	TSoftObjectPtr<UTexture2D> ArmorFallback;

	/** Fallback icon for consumables without a Thumbnail. */
	UPROPERTY(Config, EditAnywhere, Category = "Fallback Icons", meta = (AllowedClasses = "/Script/Engine.Texture2D"))
	TSoftObjectPtr<UTexture2D> ConsumableFallback;

	/** Fallback icon for everything else (materials, quest items, keys ...) without a Thumbnail. */
	UPROPERTY(Config, EditAnywhere, Category = "Fallback Icons", meta = (AllowedClasses = "/Script/Engine.Texture2D"))
	TSoftObjectPtr<UTexture2D> OtherFallback;
};
