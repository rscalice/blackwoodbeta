// Blackwood Hollow - armor inventory item (Narrative Inventory / Equipment)
// Target: Unreal Engine 5.8 (C++)
//
// Derives from Narrative's UEquippableItem_Clothing (slot per CLASS via EquippableSlot, optional ClothingMesh). Armor
// pieces go in Narrative's Torso / Helmet / Hands / Legs / Feet slots (see EBH_EquipSlot mapping in BH_EquipmentTypes.h);
// set EquippableSlot in the Blueprint class defaults (default here: ES_Torso).
// While the item is active (equipped) UBH_LoadoutComponent applies its three bonus floats through UAH_GE_EquipmentStatMod, server side,
// regardless of the current weapon stance. No weight classes / stamina-regen effects (design on hold).

#pragma once

#include "CoreMinimal.h"
#include "EquippableItem.h"
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
};
