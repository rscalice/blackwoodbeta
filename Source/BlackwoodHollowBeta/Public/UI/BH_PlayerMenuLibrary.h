// Blackwood Hollow - Player Menu helpers (Phase 13G-0)
// Target: Unreal Engine 5.8 (C++), Narrative Inventory/Equipment + GAS
//
// Local, read-only helpers for the Player Menu widgets (Inventory tab, item detail, stats block). Nothing here changes game state and
// nothing replicates; every function works on any machine from the replicated inventory / attributes.
//
//   * slot display names ("Helm", "Gloves", "Chest", "Pants", "Boots", "Main Hand A" ...),
//   * per-stat deltas an item would cause against what it replaces (Attack Power, Defense, Max Stamina),
//   * bag usage (slots and weight), Heart-Fragment shielding, item category and icon with a per-category fallback,
//   * the pure rules the server also enforces for the equip / drop RPCs (UBH_PlayerMenuComponent), so the UI can grey out
//     what the server would refuse.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "Items/BH_EquipmentTypes.h"
#include "BH_PlayerMenuLibrary.generated.h"

class ABH_PlayerState;
class APawn;
class UNarrativeItem;
class UTexture2D;

/** Coarse item category, used to pick the placeholder icon and to group the bag. */
UENUM(BlueprintType)
enum class EBH_ItemCategory : uint8
{
	Weapon,
	Armor,
	Consumable,
	Other
};

/** The three stats weapon and armor items can carry (UBH_WeaponItem / UBH_ArmorItem bonus floats). */
USTRUCT(BlueprintType)
struct BLACKWOODHOLLOWBETA_API FBH_EquipStatBonuses
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float AttackPower = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float Defense = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
	float MaxStamina = 0.f;
};

/** One row of the item detail panel: the player's current total, the total after equipping, and the difference (green when > 0, red when < 0). */
USTRUCT(BlueprintType)
struct BLACKWOODHOLLOWBETA_API FBH_ItemStatDelta
{
	GENERATED_BODY()

	/** "AttackPower", "Defense" or "MaxStamina" (same ids as BH_EquipStatKeys without the Bonus suffix). */
	UPROPERTY(BlueprintReadOnly, Category = "Stats")
	FName StatId;

	UPROPERTY(BlueprintReadOnly, Category = "Stats")
	FText Label;

	/** The pawn's current attribute value (0 when the pawn has no ability system yet). */
	UPROPERTY(BlueprintReadOnly, Category = "Stats")
	float Current = 0.f;

	/** The attribute value once the item is equipped (Current - what it replaces + what it adds). */
	UPROPERTY(BlueprintReadOnly, Category = "Stats")
	float AfterEquip = 0.f;

	/** AfterEquip - Current. */
	UPROPERTY(BlueprintReadOnly, Category = "Stats")
	float Delta = 0.f;
};

UCLASS()
class BLACKWOODHOLLOWBETA_API UBH_PlayerMenuLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// -- Slots ---------------------------------------------------------------------------------------------------------

	/** Player-facing slot name: Helm, Chest, Gloves, Pants, Boots, Main Hand A, Off Hand A, Main Hand B, Off Hand B. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|PlayerMenu")
	static FText GetEquipSlotDisplayName(EBH_EquipSlot Slot);

	/**
	 * The slot rule the server applies before any equip: weapon set B is refused while locked. Armor slots and set A always pass.
	 * @param OutReason empty when allowed, else the toast text.
	 */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|PlayerMenu")
	static bool IsEquipSlotAllowed(bool bSetBUnlocked, EBH_EquipSlot Slot, FText& OutReason);

	/** IsEquipSlotAllowed for a player (reads the replicated set B gate). A null player state counts as locked for set B. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|PlayerMenu")
	static bool CanEquipToSlot(const ABH_PlayerState* PlayerState, EBH_EquipSlot Slot, FText& OutReason);

	// -- Items ---------------------------------------------------------------------------------------------------------

	/** True while the item is equipped (Narrative's active flag; always false for items that cannot be activated). */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|PlayerMenu")
	static bool IsItemEquipped(const UNarrativeItem* Item);

	/** The item's gameplay tag that marks quest items (Item.Quest). Quest items can never be dropped. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|PlayerMenu")
	static FGameplayTag GetQuestItemTag();

	/**
	 * The drop rule the server applies: equipped items, quest items (Item.Quest) and items Narrative says cannot be removed are refused.
	 * @param OutReason empty when the item may be dropped, else the toast text.
	 */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|PlayerMenu")
	static bool CanDropItem(const UNarrativeItem* Item, FText& OutReason);

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|PlayerMenu")
	static EBH_ItemCategory GetItemCategory(TSubclassOf<UNarrativeItem> ItemClass);

	/**
	 * The item's Narrative Thumbnail, or the fallback for its category when it has none (or it cannot be loaded). Any fallback may be null.
	 * Icons are loaded synchronously (they are small UI textures).
	 */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|PlayerMenu")
	static UTexture2D* GetItemIcon(TSubclassOf<UNarrativeItem> ItemClass, UTexture2D* WeaponFallback, UTexture2D* ArmorFallback, UTexture2D* ConsumableFallback, UTexture2D* OtherFallback);

	/** Attack Power / Defense / Max Stamina bonuses of a weapon or armor item. @return false (and zeros) for any other item. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|PlayerMenu")
	static bool GetItemBonuses(const UNarrativeItem* Item, FBH_EquipStatBonuses& OutBonuses);

	/**
	 * Per-stat rows (Attack Power, Defense, Max Stamina) for equipping Item into TargetSlot of Pawn's player.
	 * "Replaced" is everything the equip would switch off: the current occupant of the slot, and for a two-handed weapon the off hand of that set.
	 * Weapon bonuses only count while their set is the active one (the stance does not change by equipping into the other set), so an
	 * equip into the inactive set shows zero deltas. Armor always counts. An item that is already equipped adds nothing new.
	 * @return false when Item is not a weapon or armor piece, or Pawn / Item is null (OutRows is then empty).
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|PlayerMenu")
	static bool GetItemStatDeltas(APawn* Pawn, UNarrativeItem* Item, EBH_EquipSlot TargetSlot, TArray<FBH_ItemStatDelta>& OutRows);

	/** The pure maths behind GetItemStatDeltas: AfterEquip = Current - Replaced + Incoming for each of the three stats. */
	static void BuildStatDeltaRows(const FBH_EquipStatBonuses& CurrentTotals, const FBH_EquipStatBonuses& Replaced, const FBH_EquipStatBonuses& Incoming, TArray<FBH_ItemStatDelta>& OutRows);

	// -- Bag -----------------------------------------------------------------------------------------------------------

	/**
	 * Inventory slots in use / total. Narrative's capacity counts item STACKS (one slot each, equipped items included), the same limit the
	 * pickup code and the "Inventory full" toast use. @return false when there is no inventory yet.
	 */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|PlayerMenu")
	static bool GetBagUsage(const ABH_PlayerState* PlayerState, int32& OutUsed, int32& OutCapacity);

	/** Carried weight / weight capacity in kg. Narrative enforces this limit in addition to the slot count. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|PlayerMenu")
	static bool GetBagWeight(const ABH_PlayerState* PlayerState, float& OutWeight, float& OutWeightCapacity);

	// -- Heart-Fragment ------------------------------------------------------------------------------------------------

	/** Fraction (0..1) of Blight build-up the Heart-Fragment shields, Fracture-aware (0.15 normally, 0.1125 while fractured). 0 for a pawn without the component. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|PlayerMenu")
	static float GetFragmentShielding(const APawn* Pawn);
};
