// Blackwood Hollow - Weapon mesh attachment types
// Target: Unreal Engine 5.8 (C++)
//
// Data types used by UBH_CombatFunctionLibrary's weapon attachment functions
// and UBH_WeaponLoadoutDataAsset. A loadout describes which Static/Skeletal
// meshes get spawned onto the character's weapon sockets for a given GASP
// Enum_OverlayPose entry (keyed by display name, e.g. "SwordAndShield").

#pragma once

#include "CoreMinimal.h"
#include "BH_WeaponTypes.generated.h"

class UStaticMesh;
class USkeletalMesh;

/** Which hand/socket a weapon mesh attaches to. */
UENUM(BlueprintType)
enum class EBH_WeaponSlot : uint8
{
	/** Right hand -- attaches to 'weapon_r_socket'. */
	MainHand UMETA(DisplayName = "Main Hand (weapon_r_socket)"),

	/** Left forearm -- attaches to 'shield_l_socket'. */
	OffHand UMETA(DisplayName = "Off Hand (shield_l_socket)"),
};

/**
 * One mesh to attach to one slot. Set EITHER StaticMesh OR SkeletalMesh
 * (if both are set, SkeletalMesh wins). Leave both empty for "nothing in this hand".
 */
USTRUCT(BlueprintType)
struct BLACKWOODHOLLOWBETA_API FBH_WeaponMeshSlot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	TSoftObjectPtr<UStaticMesh> StaticMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	TSoftObjectPtr<USkeletalMesh> SkeletalMesh;

	/** Optional socket override. None = the slot's default socket (weapon_r_socket / shield_l_socket). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	FName SocketOverride = NAME_None;

	/** Offset applied relative to the socket after attaching (grip/orientation tuning). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	FTransform RelativeTransform = FTransform::Identity;

	bool HasMesh() const { return !StaticMesh.IsNull() || !SkeletalMesh.IsNull(); }
};

/** Full set of meshes for one overlay pose (e.g. sword in MainHand + shield in OffHand). */
USTRUCT(BlueprintType)
struct BLACKWOODHOLLOWBETA_API FBH_OverlayWeaponLoadout
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	FBH_WeaponMeshSlot MainHand;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	FBH_WeaponMeshSlot OffHand;
};
