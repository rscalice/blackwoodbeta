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

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Items/BH_EquipmentTypes.h"
#include "BH_LoadoutComponent.generated.h"

class UBH_WeaponItem;
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

	/** Recomputes AvailableStances from the equipped items; broadcasts OnAvailableStancesChanged if it changed. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Loadout")
	void EvaluateAvailableStances();

	/** Server only: applies/removes the per-item stat-mod effects so only the active set's weapons contribute. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Loadout")
	void RefreshStatMods();

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
	void ActivateDeferred(TWeakObjectPtr<UBH_WeaponItem> Item);
	UAbilitySystemComponent* GetOwnerASC() const;
	void ForceInventoryNetUpdate() const;

	UPROPERTY(Transient)
	TObjectPtr<UEquipmentComponent> Equipment;

	FTimerHandle ReconcileTimer;
	bool bStarterGranted = false;
};
