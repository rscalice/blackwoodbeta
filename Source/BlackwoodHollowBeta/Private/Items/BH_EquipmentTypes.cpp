// Blackwood Hollow - Phase 8C equipment types (implementation)

#include "Items/BH_EquipmentTypes.h"
#include "Items/BH_WeaponItem.h"
#include "Combat/BH_LoadoutComponent.h"
#include "GameFramework/Pawn.h"

EEquippableSlot UBH_EquipmentLibrary::ToEquippableSlot(EBH_EquipSlot Slot)
{
	switch (Slot)
	{
	case EBH_EquipSlot::Head:          return EEquippableSlot::ES_Helmet;
	case EBH_EquipSlot::Chest:         return EEquippableSlot::ES_Torso;
	case EBH_EquipSlot::Arms:          return EEquippableSlot::ES_Hands;
	case EBH_EquipSlot::Legs:          return EEquippableSlot::ES_Legs;
	case EBH_EquipSlot::Feet:          return EEquippableSlot::ES_Feet;
	case EBH_EquipSlot::Weapon_Main_A: return EEquippableSlot::ES_Weapon;
	case EBH_EquipSlot::Weapon_Off_A:  return EEquippableSlot::ES_Holster;
	case EBH_EquipSlot::Weapon_Main_B: return EEquippableSlot::ES_Custom1;
	case EBH_EquipSlot::Weapon_Off_B:  return EEquippableSlot::ES_Custom2;
	default:                           return EEquippableSlot::ES_Weapon;
	}
}

bool UBH_EquipmentLibrary::FromEquippableSlot(EEquippableSlot Slot, EBH_EquipSlot& OutSlot)
{
	switch (Slot)
	{
	case EEquippableSlot::ES_Helmet:  OutSlot = EBH_EquipSlot::Head;          return true;
	case EEquippableSlot::ES_Torso:   OutSlot = EBH_EquipSlot::Chest;         return true;
	case EEquippableSlot::ES_Hands:   OutSlot = EBH_EquipSlot::Arms;          return true;
	case EEquippableSlot::ES_Legs:    OutSlot = EBH_EquipSlot::Legs;          return true;
	case EEquippableSlot::ES_Feet:    OutSlot = EBH_EquipSlot::Feet;          return true;
	case EEquippableSlot::ES_Weapon:  OutSlot = EBH_EquipSlot::Weapon_Main_A; return true;
	case EEquippableSlot::ES_Holster: OutSlot = EBH_EquipSlot::Weapon_Off_A;  return true;
	case EEquippableSlot::ES_Custom1: OutSlot = EBH_EquipSlot::Weapon_Main_B; return true;
	case EEquippableSlot::ES_Custom2: OutSlot = EBH_EquipSlot::Weapon_Off_B;  return true;
	default:                          return false;
	}
}

bool UBH_EquipmentLibrary::IsWeaponSlot(EBH_EquipSlot Slot)
{
	return Slot == EBH_EquipSlot::Weapon_Main_A || Slot == EBH_EquipSlot::Weapon_Off_A
		|| Slot == EBH_EquipSlot::Weapon_Main_B || Slot == EBH_EquipSlot::Weapon_Off_B;
}

bool UBH_EquipmentLibrary::IsOffHandSlot(EBH_EquipSlot Slot)
{
	return Slot == EBH_EquipSlot::Weapon_Off_A || Slot == EBH_EquipSlot::Weapon_Off_B;
}

EBH_LoadoutSet UBH_EquipmentLibrary::GetSlotLoadoutSet(EBH_EquipSlot Slot)
{
	return (Slot == EBH_EquipSlot::Weapon_Main_B || Slot == EBH_EquipSlot::Weapon_Off_B) ? EBH_LoadoutSet::B : EBH_LoadoutSet::A;
}

EBH_EquipSlot UBH_EquipmentLibrary::GetMainSlot(EBH_LoadoutSet Set)
{
	return Set == EBH_LoadoutSet::A ? EBH_EquipSlot::Weapon_Main_A : EBH_EquipSlot::Weapon_Main_B;
}

EBH_EquipSlot UBH_EquipmentLibrary::GetOffSlot(EBH_LoadoutSet Set)
{
	return Set == EBH_LoadoutSet::A ? EBH_EquipSlot::Weapon_Off_A : EBH_EquipSlot::Weapon_Off_B;
}

bool UBH_EquipmentLibrary::EquipWeaponToSlot(APawn* Pawn, UBH_WeaponItem* Item, EBH_EquipSlot Slot, FText& OutReason)
{
	if (UBH_LoadoutComponent* Loadout = UBH_LoadoutComponent::FindLoadoutComponent(Pawn))
	{
		return Loadout->EquipWeaponToSlot(Item, Slot, OutReason);
	}
	OutReason = FText::FromString(TEXT("Pawn has no loadout component"));
	return false;
}

bool UBH_EquipmentLibrary::UnequipWeaponSlot(APawn* Pawn, EBH_EquipSlot Slot, FText& OutReason)
{
	if (UBH_LoadoutComponent* Loadout = UBH_LoadoutComponent::FindLoadoutComponent(Pawn))
	{
		return Loadout->UnequipWeaponSlot(Slot, OutReason);
	}
	OutReason = FText::FromString(TEXT("Pawn has no loadout component"));
	return false;
}
