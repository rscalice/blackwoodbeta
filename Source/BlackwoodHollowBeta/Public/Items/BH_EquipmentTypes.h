// Blackwood Hollow - Phase 8C equipment types (slots, grip types, loadout sets)
// Target: Unreal Engine 5.8 (C++), Narrative Inventory 2.x (NarrativeInventory + NarrativeEquipment modules)
//
// Narrative's EEquippableSlot is a fixed engine-plugin enum (Torso, Legs, Feet, Helmet, Hands, Backpack,
// Necklace, Holster, Weapon, Custom1..5) and a UEquipmentComponent keeps exactly one item per entry. We
// want 4 weapon slots (two loadout sets, A and B, each with a main and an off hand), so our own
// EBH_EquipSlot is mapped onto the Narrative slots that the game does not otherwise use:
//
//   EBH_EquipSlot     ->  EEquippableSlot
//   Head              ->  ES_Helmet
//   Chest             ->  ES_Torso
//   Arms              ->  ES_Hands
//   Legs              ->  ES_Legs
//   Feet              ->  ES_Feet
//   Weapon_Main_A     ->  ES_Weapon
//   Weapon_Off_A      ->  ES_Holster
//   Weapon_Main_B     ->  ES_Custom1
//   Weapon_Off_B      ->  ES_Custom2
//
// Use UBH_EquipmentLibrary::ToEquippableSlot / FromEquippableSlot; never hard-code the mapping elsewhere.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EquippableItem.h"
#include "ActiveGameplayEffectHandle.h"
#include "BH_EquipmentTypes.generated.h"

class APawn;
class UAbilitySystemComponent;
class UBH_WeaponItem;
struct FNarrativeItemStat;

/**
 * StringVariable keys used by the Narrative item Stats rows (match the UPROPERTY names on weapon and armor items).
 * Plain constexpr literals: no static-init-order globals.
 */
namespace BH_EquipStatKeys
{
	inline constexpr const TCHAR* AttackPower = TEXT("AttackPowerBonus");
	inline constexpr const TCHAR* Defense = TEXT("DefenseBonus");
	inline constexpr const TCHAR* MaxStamina = TEXT("MaxStaminaBonus");
}

/** Our logical equipment slots (see the mapping table above). */
UENUM(BlueprintType)
enum class EBH_EquipSlot : uint8
{
	Head,
	Chest,
	Arms,
	Legs,
	Feet,
	Weapon_Main_A,
	Weapon_Off_A,
	Weapon_Main_B,
	Weapon_Off_B
};

/** How a weapon is held: decides which slots it may occupy and whether it locks the off hand. */
UENUM(BlueprintType)
enum class EBH_WeaponGripType : uint8
{
	OneHanded,
	TwoHanded,
	OffHand
};

/** The two weapon loadout sets the player can swap between by changing stance. */
UENUM(BlueprintType)
enum class EBH_LoadoutSet : uint8
{
	A,
	B
};

UCLASS()
class BLACKWOODHOLLOWBETA_API UBH_EquipmentLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Equipment")
	static EEquippableSlot ToEquippableSlot(EBH_EquipSlot Slot);

	/** @return false if Slot is not one of the Narrative slots we map to. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Equipment")
	static bool FromEquippableSlot(EEquippableSlot Slot, EBH_EquipSlot& OutSlot);

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Equipment")
	static bool IsWeaponSlot(EBH_EquipSlot Slot);

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Equipment")
	static bool IsOffHandSlot(EBH_EquipSlot Slot);

	/** Loadout set (A/B) a weapon slot belongs to. Non-weapon slots return A. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Equipment")
	static EBH_LoadoutSet GetSlotLoadoutSet(EBH_EquipSlot Slot);

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Equipment")
	static EBH_EquipSlot GetMainSlot(EBH_LoadoutSet Set);

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Equipment")
	static EBH_EquipSlot GetOffSlot(EBH_LoadoutSet Set);

	// -- Server-authoritative equip helpers (thin wrappers over UBH_LoadoutComponent) --------------

	/**
	 * Equips Item (which must be in the pawn's PlayerState inventory) into Slot. Server only.
	 * Two-handed weapons empty and lock the off slot of their set; equipping into a locked off slot fails.
	 * @param OutReason human-readable reason when this returns false.
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Equipment")
	static bool EquipWeaponToSlot(APawn* Pawn, UBH_WeaponItem* Item, EBH_EquipSlot Slot, FText& OutReason);

	/** Unequips whatever weapon is in Slot (it stays in the inventory). Server only. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Equipment")
	static bool UnequipWeaponSlot(APawn* Pawn, EBH_EquipSlot Slot, FText& OutReason);

	// -- Equipment stat bonuses (plain C++, NOT reflected) -----------------------------------------
	// Weapon and armor items own three float UPROPERTYs (AttackPowerBonus / DefenseBonus / MaxStaminaBonus) and show
	// them through Narrative's item Stats (UNarrativeItem::Stats + GetStringVariable). ApplyStatMod / RemoveStatMod are
	// the single place that builds and applies/removes UAH_GE_EquipmentStatMod through its SetByCaller tags
	// (Data.Equip.AttackPower / Defense / MaxStamina).

	/** Appends the "Attack Power" / "Defense" / "Max Stamina" rows to a Narrative item's Stats array. */
	static void AddStatRows(TArray<FNarrativeItemStat>& OutStats);

	/** Resolves a StringVariable key to its formatted value. @return false if VariableName is not one of the three keys. */
	static bool GetStatString(const FString& VariableName, float AttackPower, float Defense, float MaxStamina, FString& OutValue);

	/** Server only: applies UAH_GE_EquipmentStatMod to ASC with the three bonuses (SourceObject = Source). @return the active handle (invalid on failure). */
	static FActiveGameplayEffectHandle ApplyStatMod(UAbilitySystemComponent* ASC, UObject* Source, float AttackPower, float Defense, float MaxStamina);

	/** Server only: removes the effect behind Handle (if valid) and resets it. */
	static void RemoveStatMod(UAbilitySystemComponent* ASC, FActiveGameplayEffectHandle& Handle);
};
