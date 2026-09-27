// Blackwood Hollow - Overlay pose -> weapon mesh loadout table (implementation)

#include "Combat/BH_WeaponLoadoutDataAsset.h"

bool UBH_WeaponLoadoutDataAsset::FindLoadout(FName OverlayPoseDisplayName, FBH_OverlayWeaponLoadout& OutLoadout) const
{
	if (const FBH_OverlayWeaponLoadout* Found = LoadoutsByOverlayPose.Find(OverlayPoseDisplayName))
	{
		OutLoadout = *Found;
		return true;
	}
	return false;
}
