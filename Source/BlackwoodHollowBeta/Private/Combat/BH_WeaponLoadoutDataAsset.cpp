// Blackwood Hollow - Overlay pose -> weapon mesh loadout table (implementation)

#include "Combat/BH_WeaponLoadoutDataAsset.h"
#include "AbilitySystem/BH_GameplayTags.h"

FBH_OverlayWeaponLoadout* UBH_WeaponLoadoutDataAsset::FindLoadoutEntry(FName OverlayPoseDisplayName)
{
	const FGameplayTag Stance = BH_Stance::FromLegacyName(OverlayPoseDisplayName);
	if (Stance.IsValid())
	{
		if (FBH_OverlayWeaponLoadout* ByTag = LoadoutsByStance.Find(Stance))
		{
			return ByTag;
		}
	}
	return LoadoutsByOverlayPose.Find(OverlayPoseDisplayName);
}

bool UBH_WeaponLoadoutDataAsset::FindLoadout(FName OverlayPoseDisplayName, FBH_OverlayWeaponLoadout& OutLoadout) const
{
	if (const FBH_OverlayWeaponLoadout* Found = const_cast<UBH_WeaponLoadoutDataAsset*>(this)->FindLoadoutEntry(OverlayPoseDisplayName))
	{
		OutLoadout = *Found;
		return true;
	}
	return false;
}
