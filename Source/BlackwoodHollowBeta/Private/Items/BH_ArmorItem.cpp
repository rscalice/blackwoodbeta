// Blackwood Hollow - armor inventory item (implementation)

#include "Items/BH_ArmorItem.h"
#include "Items/BH_EquipmentTypes.h"

UBH_ArmorItem::UBH_ArmorItem()
{
	bCanActivate = true;
	bToggleActiveOnUse = true;
	bStackable = false;
	EquippableSlot = EEquippableSlot::ES_Torso;
	UseActionText = NSLOCTEXT("BHArmorItem", "UseActionText", "Equip");

	// Keep Narrative's default Weight / Quantity rows; append ours.
	UBH_EquipmentLibrary::AddStatRows(Stats);
}

FString UBH_ArmorItem::GetStringVariable_Implementation(const FString& VariableName)
{
	FString Value;
	if (UBH_EquipmentLibrary::GetStatString(VariableName, AttackPowerBonus, DefenseBonus, MaxStaminaBonus, Value))
	{
		return Value;
	}
	return Super::GetStringVariable_Implementation(VariableName);
}

FString UBH_ArmorItem::DescribeBonuses() const
{
	const TCHAR* WeightName = (WeightClass == EBH_ArmorWeightClass::Heavy) ? TEXT("Heavy") : (WeightClass == EBH_ArmorWeightClass::Medium) ? TEXT("Medium") : TEXT("Cloth");
	return FString::Printf(TEXT("%s AP%+.0f DEF%+.0f MaxSTA%+.0f"), WeightName, AttackPowerBonus, DefenseBonus, MaxStaminaBonus);
}

bool UBH_ArmorItem::GetArmorSlot(EBH_EquipSlot& OutSlot) const
{
	if (!UBH_EquipmentLibrary::FromEquippableSlot(EquippableSlot, OutSlot))
	{
		return false;
	}
	return !UBH_EquipmentLibrary::IsWeaponSlot(OutSlot);
}

FString UBH_ArmorItem::GetFriendlyName() const
{
	if (!ArmorName.IsEmpty())
	{
		return ArmorName.ToString();
	}
	if (!DisplayName.IsEmpty())
	{
		return DisplayName.ToString();
	}
	return GetClass()->GetName();
}
