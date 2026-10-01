// Blackwood Hollow - weapon inventory item (Narrative Inventory / Equipment)
// Target: Unreal Engine 5.8 (C++)
//
// UEquippableItem keeps its slot in a per-CLASS default (EquippableSlot, EditDefaultsOnly). We need the same
// weapon to be usable in loadout A or B, main or off hand, so each INSTANCE carries a replicated
// AssignedLoadout + bEquipToOffHand and rewrites EquippableSlot from them right before it is activated
// (equipped) -- on the server and on every client (Activated runs from OnRep_bActive there).
//
// Slot rules (GetTargetSlot):
//   TwoHanded           -> Weapon_Main_<set>
//   OneHanded           -> Weapon_Main_<set>, or Weapon_Off_<set> when bEquipToOffHand (dual wield)
//   OffHand (shield)    -> Weapon_Off_<set>
// Stats (AttackPowerBonus / DefenseBonus / MaxStaminaBonus) are applied through UAH_GE_EquipmentStatMod by
// UBH_LoadoutComponent, for the ACTIVE stance's loadout set only.

#pragma once

#include "CoreMinimal.h"
#include "EquippableItem.h"
#include "ActiveGameplayEffectHandle.h"
#include "Items/BH_EquipmentTypes.h"
#include "BH_WeaponItem.generated.h"

class UStaticMesh;

UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API UBH_WeaponItem : public UEquippableItem
{
	GENERATED_BODY()

public:
	UBH_WeaponItem();

	/** How the weapon is held (decides slot eligibility and the two-handed off-hand lock). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	EBH_WeaponGripType GripType = EBH_WeaponGripType::OneHanded;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	FText WeaponName;

	/** Informational for now: weapon visuals stay driven by DA_WeaponLoadouts per stance. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	TObjectPtr<UStaticMesh> Mesh;

	// -- Stat bonuses applied (additively, via GAS) while this weapon is equipped in the active loadout set --
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Stats")
	float AttackPowerBonus = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Stats")
	float DefenseBonus = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Stats")
	float MaxStaminaBonus = 0.f;

	/** Loadout set (A/B) this instance equips into. Set by UBH_LoadoutComponent before equipping. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Weapon|Loadout")
	EBH_LoadoutSet AssignedLoadout = EBH_LoadoutSet::A;

	/** OneHanded only: equip into the off-hand slot of AssignedLoadout (dual wield). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Weapon|Loadout")
	bool bEquipToOffHand = false;

	/** Server-only: the stat-mod effect currently applied for this item (invalid when none). */
	FActiveGameplayEffectHandle StatModHandle;

	/** The slot this instance will occupy with its current AssignedLoadout / bEquipToOffHand. */
	UFUNCTION(BlueprintPure, Category = "Weapon|Loadout")
	EBH_EquipSlot GetTargetSlot() const;

	/** Sets AssignedLoadout / bEquipToOffHand from a weapon slot (does not equip). Server only. @return false if the slot is invalid for this grip type. */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Loadout")
	bool AssignToSlot(EBH_EquipSlot Slot);

	/** Same stat bonuses as one struct-free line, for logs/UI. */
	UFUNCTION(BlueprintPure, Category = "Weapon")
	FString DescribeBonuses() const;

	/** Display name used by logs: WeaponName if set, else the item DisplayName, else the class name. */
	UFUNCTION(BlueprintPure, Category = "Weapon")
	FString GetFriendlyName() const;

	virtual bool CanUse_Implementation() const override;

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Activated_Implementation() override;

private:
	/** Writes EquippableSlot (Narrative's per-class slot) from the per-instance assignment. */
	void RefreshEquippableSlot();
};
