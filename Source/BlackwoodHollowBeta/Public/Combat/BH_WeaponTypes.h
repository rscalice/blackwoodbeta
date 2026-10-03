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
class UTexture2D;
class UGameplayAbility;

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

	/** Socket the weapon rides on while the weapon is sheathed (e.g. weapon_back_socket). None = it stays in the hand. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Sheathed")
	FName SheathedSocket = NAME_None;

	/** Offset relative to SheathedSocket while sheathed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Sheathed")
	FTransform SheathedRelativeTransform = FTransform::Identity;

	/**
	 * Per-weapon blade line for UANS_MeleeHitbox, used when the mesh has no weapon_root / weapon_tip sockets.
	 * BladeRootLocal / BladeTipLocal are in the weapon COMPONENT's local space (guard side / tip side).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Blade")
	bool bUseBladeOverride = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Blade", meta = (EditCondition = "bUseBladeOverride"))
	FVector BladeRootLocal = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Blade", meta = (EditCondition = "bUseBladeOverride"))
	FVector BladeTipLocal = FVector::ZeroVector;

	/**
	 * Two-handed grip: the left hand is IK'd onto SecondaryGripLocal (weapon COMPONENT local space) by the layer-blending
	 * anim BP (see UBH_CombatFunctionLibrary::GetSecondaryGripIKTarget). SecondaryGripRotLocal is an optional palm orientation.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Grip")
	bool bTwoHandedGrip = false;

	/** Where the right (main) hand holds the weapon, weapon COMPONENT local space. Pivot for the two-hand weapon aim. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Grip", meta = (EditCondition = "bTwoHandedGrip"))
	FVector PrimaryGripLocal = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Grip", meta = (EditCondition = "bTwoHandedGrip"))
	FVector SecondaryGripLocal = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Grip", meta = (EditCondition = "bTwoHandedGrip"))
	FRotator SecondaryGripRotLocal = FRotator::ZeroRotator;

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

	/** HUD stance emblem for this pose (UBH_VitalsClusterWidget). An entry may set ONLY this (empty weapon slots = unarmed). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stance")
	TObjectPtr<UTexture2D> StanceIcon;

	/** Melee combo ability used while this stance is active (UBH_CombatFunctionLibrary::GetMeleeAbilityForPose). Empty = caller's fallback. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stance")
	TSubclassOf<UGameplayAbility> MeleeAbility;
};
