// Blackwood Hollow - Phase 11E armor visual data (shared by UBH_ArmorItem and the starting outfit in UBH_RPGSettings)
// Target: Unreal Engine 5.8 (C++)
//
// FBH_ArmorVisual describes what ONE armor slot looks like on the visible MetaHuman body (see UBH_ArmorVisualComponent):
//   * SkeletalMesh: leader-posed to the visual body (same bone names / reference pose as SK_BH_TallBody). One combined mesh may be
//     shared by several slots: HiddenMaterialSlots lists the material indices NOT to draw (e.g. the Villager mesh: slot 0 body, 1 tunic,
//     2 shoes, 3 pants, 4 belt).
//   * StaticMesh: used when SkeletalMesh is empty. Attached to StaticMeshBone of the visual body (e.g. the leather cap on "head").
//   * Everything here is cosmetic and built locally on every machine; nothing in it is replicated.

#pragma once

#include "CoreMinimal.h"
#include "Items/BH_EquipmentTypes.h"
#include "BH_ArmorVisualTypes.generated.h"

class UBH_ArmorItem;
class UMaterialInterface;
class USkeletalMesh;
class UStaticMesh;

/** One material slot replaced on an armor piece. */
USTRUCT(BlueprintType)
struct FBH_ArmorMaterialOverride
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Armor", meta = (ClampMin = "0"))
	int32 SlotIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Armor")
	TSoftObjectPtr<UMaterialInterface> Material;
};

USTRUCT(BlueprintType)
struct FBH_ArmorVisual
{
	GENERATED_BODY()

	/** Leader-posed to the visual body. Takes priority over StaticMesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Armor")
	TSoftObjectPtr<USkeletalMesh> SkeletalMesh;

	/** Used when SkeletalMesh is empty (helmets / caps that are static meshes). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Armor")
	TSoftObjectPtr<UStaticMesh> StaticMesh;

	/** Bone of the visual body the static mesh follows. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Armor")
	FName StaticMeshBone = TEXT("head");

	/** Extra transform for the static mesh (in component space when bStaticMeshInComponentSpace, else relative to the bone). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Armor")
	FTransform StaticMeshOffset;

	/** true: the static mesh was modelled in place on the character (origin at the feet, like the Villager leather cap), so the bone's
	 *  reference pose is removed automatically and it sits right on the bone. false: the mesh is already in bone space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Armor")
	bool bStaticMeshInComponentSpace = true;

	/** Replaces these material slots of the piece's mesh. Empty = the mesh's own materials. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Armor")
	TArray<FBH_ArmorMaterialOverride> MaterialOverrides;

	/** Material slots of the piece's skeletal mesh that are NOT drawn (shared / combined meshes). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Armor")
	TArray<int32> HiddenMaterialSlots;

	/** Material slots of the VISUAL BODY hidden while this piece is worn (stops skin poking through). Aggregated over all worn pieces. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Armor")
	TArray<int32> HiddenBodyMaterialSlots;

	/** The hair groom is hidden while this piece is worn (helmets, caps). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Armor")
	bool bHidesHair = false;

	bool HasMesh() const { return !SkeletalMesh.IsNull() || !StaticMesh.IsNull(); }
};

/** One piece of the starting outfit: shown in Slot whenever no armor is equipped there. */
USTRUCT(BlueprintType)
struct FBH_StartingOutfitPiece
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Armor")
	EBH_EquipSlot Slot = EBH_EquipSlot::Chest;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Armor")
	FBH_ArmorVisual Visual;
};

/** A named set of armor item classes the bh.Armor.Give debug command grants and equips. */
USTRUCT(BlueprintType)
struct FBH_ArmorDebugSet
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Armor")
	FName SetName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Armor")
	TArray<TSoftClassPtr<UBH_ArmorItem>> Pieces;
};
