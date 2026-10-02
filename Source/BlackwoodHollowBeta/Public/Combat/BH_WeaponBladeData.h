// Blackwood Hollow - per-weapon blade line carried on the spawned weapon component
// Target: Unreal Engine 5.8 (C++)

#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"
#include "BH_WeaponBladeData.generated.h"

/**
 * Blade root/tip in the weapon component's local space, copied from FBH_WeaponMeshSlot by
 * UBH_CombatFunctionLibrary::AttachWeaponMesh onto the spawned component (as asset user data), so it
 * lives and dies with the component and survives every re-equip. Read by UANS_MeleeHitbox.
 */
UCLASS()
class BLACKWOODHOLLOWBETA_API UBH_WeaponBladeData : public UAssetUserData
{
	GENERATED_BODY()

public:
	/** true when RootLocal/TipLocal are a real blade override (this object may also exist only to carry the grip data). */
	UPROPERTY()
	bool bHasBladeLine = false;

	UPROPERTY()
	FVector RootLocal = FVector::ZeroVector;

	/** Two-handed grip, copied from FBH_WeaponMeshSlot (weapon component local space). */
	UPROPERTY()
	bool bTwoHandedGrip = false;

	UPROPERTY()
	FVector PrimaryGripLocal = FVector::ZeroVector;

	/** The authored relative transform to the attach socket (what the two-hand aim rotates away from and restores). */
	UPROPERTY()
	FTransform AuthoredRelative = FTransform::Identity;

	UPROPERTY()
	FVector SecondaryGripLocal = FVector::ZeroVector;

	UPROPERTY()
	FRotator SecondaryGripRotLocal = FRotator::ZeroRotator;

	UPROPERTY()
	FVector TipLocal = FVector::ZeroVector;
};
