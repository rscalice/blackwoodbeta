// Blackwood Hollow - armor inventory item (Narrative Inventory / Equipment)
// Target: Unreal Engine 5.8 (C++)
//
// Derives from Narrative's UEquippableItem_Clothing (slot per CLASS via EquippableSlot, optional ClothingMesh). Armor
// pieces go in Narrative's Torso / Helmet / Hands / Legs / Feet slots (see EBH_EquipSlot mapping in BH_EquipmentTypes.h);
// set EquippableSlot in the Blueprint class defaults (default here: ES_Torso).
// While the item is active (equipped) UBH_LoadoutComponent applies its three bonus floats through UAH_GE_EquipmentStatMod, server side,
// regardless of the current weapon stance. WeightClass (Light/Medium/Heavy): UBH_LoadoutComponent takes the heaviest equipped class and
// applies one infinite effect (stamina regen multiplier + State.Armor.Weight.* tag); see UBH_RPGSettings for the multipliers.
//
// VISUALS (Phase 11E): Visual (FBH_ArmorVisual) says what the piece looks like on the MetaHuman visual body. UBH_ArmorVisualComponent builds it
// locally on every machine. This class deliberately never sets Narrative's ClothingMesh and overrides HandleEquip / HandleUnequip as no-ops, so
// equipping can never attach anything to the hidden UEFN gameplay mesh.

#pragma once

#include "CoreMinimal.h"
#include "EquippableItem.h"
#include "Items/BH_EquipmentTypes.h"
#include "Items/BH_ArmorVisualTypes.h"
#include "BH_ArmorItem.generated.h"

UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API UBH_ArmorItem : public UEquippableItem_Clothing
{
	GENERATED_BODY()

public:
	UBH_ArmorItem();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Armor")
	FText ArmorName;

	// -- Stat bonuses applied (additively, via GAS) while this piece is equipped; shown through Narrative's item Stats --
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Armor|Stats")
	float AttackPowerBonus = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Armor|Stats")
	float DefenseBonus = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Armor|Stats")
	float MaxStaminaBonus = 0.f;

	/** Weight class. The heaviest equipped class drives the stamina regen multiplier and the dodge distance multiplier. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Armor")
	EBH_ArmorWeightClass WeightClass = EBH_ArmorWeightClass::Light;

	/** What this piece looks like on the visual body (cosmetic, built locally on every machine). Empty = the starting outfit shows in this slot. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Armor|Visual")
	FBH_ArmorVisual Visual;

	/** The BH slot this piece occupies (derived from EquippableSlot). @return false if EquippableSlot is not an armor slot. */
	UFUNCTION(BlueprintPure, Category = "Armor")
	bool GetArmorSlot(EBH_EquipSlot& OutSlot) const;

	/** The three stat bonuses as one line, for logs/UI. */
	UFUNCTION(BlueprintPure, Category = "Armor")
	FString DescribeBonuses() const;

	UFUNCTION(BlueprintPure, Category = "Armor")
	FString GetFriendlyName() const;

	/** Returns the bonus floats for the Narrative item Stats rows; falls back to Super. */
	virtual FString GetStringVariable_Implementation(const FString& VariableName) override;

protected:
	/** No-op: Narrative's clothing path would attach ClothingMesh to the hidden gameplay mesh. UBH_ArmorVisualComponent owns the visuals. */
	virtual void HandleEquip_Implementation() override;

	/** No-op, see HandleEquip_Implementation (the base would log a warning because we register no clothing slots). */
	virtual void HandleUnequip_Implementation() override;
};
