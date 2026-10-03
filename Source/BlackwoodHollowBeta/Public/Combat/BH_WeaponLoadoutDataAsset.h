// Blackwood Hollow - Overlay pose -> weapon mesh loadout table
// Target: Unreal Engine 5.8 (C++)

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Engine/DataAsset.h"
#include "Combat/BH_WeaponTypes.h"
#include "BH_WeaponLoadoutDataAsset.generated.h"

/**
 * UBH_WeaponLoadoutDataAsset
 *
 * Maps GASP Enum_OverlayPose entries to the weapon meshes that should be
 * attached when that overlay is active. Keyed by the enum entry's *display
 * name* (e.g. "SwordAndShield") rather than its ordinal, for the same reason
 * UBH_CombatFunctionLibrary::ApplyOverlayPoseByDisplayName is: GASP's enum is
 * a UserDefinedEnum in plugin content and its ordinals can shift.
 *
 * Create one instance (e.g. DA_WeaponLoadouts) and pass it to
 * UBH_CombatFunctionLibrary::EquipWeaponsForOverlayPose / EquipWeaponsForCurrentOverlayPose.
 * Overlay poses with no entry (e.g. "Default") simply equip nothing.
 */
UCLASS(BlueprintType)
class BLACKWOODHOLLOWBETA_API UBH_WeaponLoadoutDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Enum_OverlayPose display name -> meshes to attach. FName keys match case-insensitively. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loadout")
	TMap<FName, FBH_OverlayWeaponLoadout> LoadoutsByOverlayPose;

	/** Tag-keyed twin of LoadoutsByOverlayPose (Stance.Weapon.*); read first, the legacy map is the fallback. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loadout", meta = (Categories = "Stance.Weapon"))
	TMap<FGameplayTag, FBH_OverlayWeaponLoadout> LoadoutsByStance;

	/** Mutable entry for a legacy pose name: the tag map first, then the legacy map. */
	FBH_OverlayWeaponLoadout* FindLoadoutEntry(FName OverlayPoseDisplayName);

	/** @return true and fills OutLoadout if an entry exists for OverlayPoseDisplayName. */
	UFUNCTION(BlueprintPure, Category = "Loadout")
	bool FindLoadout(FName OverlayPoseDisplayName, FBH_OverlayWeaponLoadout& OutLoadout) const;
};
