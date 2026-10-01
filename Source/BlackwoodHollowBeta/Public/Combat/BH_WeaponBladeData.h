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
	UPROPERTY()
	FVector RootLocal = FVector::ZeroVector;

	UPROPERTY()
	FVector TipLocal = FVector::ZeroVector;
};
