// Blackwood Hollow - player weapon loadout (A/B sets), dynamic stance evaluation, equipment stat mods
// Target: Unreal Engine 5.8 (C++), Narrative Inventory/Equipment + GAS
//
// Lives on the player PAWN on every machine (added by UBH_CombatFunctionLibrary::SetupCombatCharacter for
// non-enemy pawns, or as a Blueprint component). It
//   * creates the pawn's UEquipmentComponent (Narrative) if the pawn has none,
//   * is the server-authoritative place to equip/unequip UBH_WeaponItem (two-handed rule included),
//   * derives AvailableStances from what is equipped in loadout A and B (locally on each machine from the
//     replicated items, so server and clients agree; OnAvailableStancesChanged fires when the list changes),
//   * on the server, applies each equipped weapon's stat bonuses through UAH_GE_EquipmentStatMod -- only for
//     the loadout set that produced the CURRENT stance (the other set's bonuses are removed),
//   * on the server, grants the configurable StarterLoadout once the pawn's PlayerState inventory exists.
//
// Stance rules per set (Main + Off):
//   Main TwoHanded                       -> "Greatsword"
//   Main OneHanded + Off OneHanded       -> "DualSword"
//   Main OneHanded + Off OffHand         -> "SwordAndShield"
//   Main OneHanded alone (no/other off)  -> "SwordAndShield" (fallback)
//   no Main                              -> no stance
// The list is de-duplicated, set A first, then B.
//
// Presets (loadout pads): ApplyLoadoutPreset swaps the whole kit in one server call -- it re-uses inventory weapons of
// the requested classes (granting only what is missing, so repeated use never piles up items), re-slots them and
// switches the stance to set A's stance. See the function comment for the replication-safe two-phase flow.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Items/BH_EquipmentTypes.h"
#include "ActiveGameplayEffectHandle.h"
#include "BH_LoadoutComponent.generated.h"

class UBH_WeaponItem;
class UBH_ArmorItem;
class UEquippableItem;
class UEquipmentComponent;
class UNarrativeInventoryComponent;
class UAbilitySystemComponent;

BLACKWOODHOLLOWBETA_API DECLARE_LOG_CATEGORY_EXTERN(LogBHLoadout, Log, All);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBH_OnAvailableStancesChanged, const TArray<FName>&, AvailableStances);

/** One starter item: granted into the inventory and (optionally) equipped into Slot. */
USTRUCT(BlueprintType)
struct FBH_StarterLoadoutEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Starter")
	TSoftClassPtr<UBH_WeaponItem> ItemClass;

	/** false = just put it in the inventory. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Starter")
	bool bEquip = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Starter", meta = (EditCondition = "bEquip"))
	EBH_EquipSlot Slot = EBH_EquipSlot::Weapon_Main_A;
};

UCLASS(ClassGroup = (BlackwoodHollow), meta = (BlueprintSpawnableComponent))
class BLACKWOODHOLLOWBETA_API UBH_LoadoutComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBH_LoadoutComponent();

	/** Items granted (server) once the PlayerState inventory is available. Edit/clear to change or remove the starter kit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|Loadout")
	TArray<FBH_StarterLoadoutEntry> StarterLoadout;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|Loadout")
	bool bGrantStarterLoadout = true;

	/** Seconds between reconcile passes (stances, stat mods, starter grant). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|Loadout", meta = (ClampMin = "0.02"))
	float ReconcileInterval = 0.1f;

	/** Stances (Enum_OverlayPose display names) available from the current equipment, A first then B, de-duplicated. */
	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|Loadout")
	TArray<FName> AvailableStances;

	UPROPERTY(BlueprintAssignable, Category = "BlackwoodHollow|Loadout")
	FBH_OnAvailableStancesChanged OnAvailableStancesChanged;

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Loadout")
	static UBH_LoadoutComponent* FindLoadoutComponent(const AActor* Actor);

	/** The pawn's PlayerState inventory (null until the PlayerState exists). */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Loadout")
	UNarrativeInventoryComponent* GetInventory() const;

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Loadout")
	UEquipmentComponent* GetEquipment() const { return Equipment; }

	/** Weapon currently equipped in Slot (derived from the replicated inventory, identical on server and clients). */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Loadout")
	UBH_WeaponItem* GetItemInSlot(EBH_EquipSlot Slot) const;

	/** True while the off hand of Set is blocked by a two-handed weapon in its main slot. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Loadout")
	bool IsOffHandLocked(EBH_LoadoutSet Set) const;

	/** Stance produced by Set's current main/off items (NAME_None if its main slot is empty). */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Loadout")
	FName GetStanceForSet(EBH_LoadoutSet Set) const;

	/** The set that produced the current overlay pose (A checked first). @return false if neither did. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Loadout")
	bool GetActiveLoadoutSet(EBH_LoadoutSet& OutSet) const;

	/** Pure check used by EquipWeaponToSlot and UBH_WeaponItem::CanUse. */
	bool ValidateEquip(const UBH_WeaponItem* Item, EBH_EquipSlot Slot, FText& OutReason) const;

	/** Server only. See UBH_EquipmentLibrary::EquipWeaponToSlot. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Loadout")
	bool EquipWeaponToSlot(UBH_WeaponItem* Item, EBH_EquipSlot Slot, FText& OutReason);

	/** Server only. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Loadout")
	bool UnequipWeaponSlot(EBH_EquipSlot Slot, FText& OutReason);

	/**
	 * Server only. Replaces the equipped weapons with Entries (same struct as StarterLoadout):
	 *   1. validates every entry up front (class loads, grip type fits the slot, no slot used twice, no off hand under a
	 *      two-handed main) so a bad preset changes nothing;
	 *   2. picks one inventory instance per entry -- an item already in the right slot, else any unequipped item of the
	 *      class, else one equipped elsewhere, else a NEW item is granted. One instance is never used for two slots, so a
	 *      DualSword preset with two swords of the same class grants/keeps two instances;
	 *   3. unequips every weapon slot whose occupant is not part of the preset, then equips each entry through
	 *      EquipWeaponToSlot (the validated path);
	 *   4. re-evaluates stances/stat mods and switches the stance to set A's stance (set B's if A is empty).
	 * Inventory items are never removed: repeated use re-uses the same instances. A weapon that has to MOVE between slots
	 * is deactivated first and equipped ~0.25 s later (the same replication rule EquipWeaponToSlot follows), so in that
	 * case steps 3b/4 run on a timer; the function still returns true once the preset is accepted.
	 * @return false (with OutReason) when not authoritative, the PlayerState inventory is not ready yet, or validation fails.
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Loadout")
	bool ApplyLoadoutPreset(const TArray<FBH_StarterLoadoutEntry>& Entries, FText& OutReason);

	/** Stance a preset yields for Set (Greatsword / DualSword / SwordAndShield), read from the item classes' default grip types. NAME_None if the set has no main weapon. Works on any machine. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Loadout")
	static FName GetStanceNameForPreset(const TArray<FBH_StarterLoadoutEntry>& Entries, EBH_LoadoutSet Set);

	/** Recomputes AvailableStances from the equipped items; broadcasts OnAvailableStancesChanged if it changed. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Loadout")
	void EvaluateAvailableStances();

	/** Server only: applies/removes the per-item stat-mod effects so only the active set's weapons contribute. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Loadout")
	void RefreshStatMods();

	/**
	 * Server only: applies/removes the stat-mod effect of every UBH_ArmorItem so exactly the equipped (active) pieces contribute,
	 * independent of the weapon stance. Called from RefreshStatMods; also drops effects of pieces that left the inventory.
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Loadout")
	void RefreshArmorStatMods();

	/**
	 * The heaviest WeightClass among the equipped (active) armor pieces. @return false when no armor is equipped
	 * (OutWeight is then Light). Works on any machine (reads the replicated inventory).
	 */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Loadout")
	bool GetEquippedArmorWeight(EBH_ArmorWeightClass& OutWeight) const;

	/** Multi-line dump of slots, stances, active set and the three stats (for console/verification). */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Loadout")
	FString DescribeState() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void HandleItemEquipped(const EEquippableSlot Slot, UEquippableItem* Equippable);

	UFUNCTION()
	void HandleItemUnequipped(const EEquippableSlot Slot, UEquippableItem* Equippable);

	void Reconcile();
	void TryGrantStarterLoadout();
	bool CommitPreset(const TArray<TPair<TWeakObjectPtr<UBH_WeaponItem>, EBH_EquipSlot>>& Plan, FText& OutReason);
	void ActivateDeferred(TWeakObjectPtr<UBH_WeaponItem> Item);
	UAbilitySystemComponent* GetOwnerASC() const;
	void ForceInventoryNetUpdate() const;

	UPROPERTY(Transient)
	TObjectPtr<UEquipmentComponent> Equipment;

	/** Server-only: armor stat-mod handles, one per equipped piece (weapons keep theirs on the item). */
	TMap<TWeakObjectPtr<UBH_ArmorItem>, FActiveGameplayEffectHandle> ArmorStatModHandles;

	/** Server-only: the single armor-weight effect (stamina regen multiplier + State.Armor.Weight.* tag) for the heaviest equipped class. */
	FActiveGameplayEffectHandle ArmorWeightHandle;

	/** Server-only: EBH_ArmorWeightClass the current ArmorWeightHandle was built for, -1 when none is applied. */
	int32 AppliedArmorWeightIndex = -1;

	FTimerHandle ReconcileTimer;
	FTimerHandle PresetTimer;
	bool bStarterGranted = false;
};
