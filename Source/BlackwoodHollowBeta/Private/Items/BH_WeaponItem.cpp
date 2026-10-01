// Blackwood Hollow - weapon inventory item (implementation)

#include "Items/BH_WeaponItem.h"
#include "Combat/BH_LoadoutComponent.h"
#include "Net/UnrealNetwork.h"
#include "Engine/StaticMesh.h"

UBH_WeaponItem::UBH_WeaponItem()
{
	bCanActivate = true;
	bToggleActiveOnUse = true;
	bStackable = false;
	EquippableSlot = EEquippableSlot::ES_Weapon;
	UseActionText = NSLOCTEXT("BHWeaponItem", "UseActionText", "Equip");
}

void UBH_WeaponItem::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UBH_WeaponItem, AssignedLoadout);
	DOREPLIFETIME(UBH_WeaponItem, bEquipToOffHand);
}

EBH_EquipSlot UBH_WeaponItem::GetTargetSlot() const
{
	bool bOff = false;
	switch (GripType)
	{
	case EBH_WeaponGripType::OffHand:   bOff = true; break;
	case EBH_WeaponGripType::OneHanded: bOff = bEquipToOffHand; break;
	case EBH_WeaponGripType::TwoHanded: bOff = false; break;
	}
	return bOff ? UBH_EquipmentLibrary::GetOffSlot(AssignedLoadout) : UBH_EquipmentLibrary::GetMainSlot(AssignedLoadout);
}

bool UBH_WeaponItem::AssignToSlot(EBH_EquipSlot Slot)
{
	if (!UBH_EquipmentLibrary::IsWeaponSlot(Slot))
	{
		return false;
	}
	const bool bOff = UBH_EquipmentLibrary::IsOffHandSlot(Slot);
	if ((GripType == EBH_WeaponGripType::TwoHanded && bOff) || (GripType == EBH_WeaponGripType::OffHand && !bOff))
	{
		return false;
	}

	AssignedLoadout = UBH_EquipmentLibrary::GetSlotLoadoutSet(Slot);
	bEquipToOffHand = (GripType == EBH_WeaponGripType::OneHanded) && bOff;
	RefreshEquippableSlot();
	MarkDirtyForReplication();
	return true;
}

void UBH_WeaponItem::RefreshEquippableSlot()
{
	EquippableSlot = UBH_EquipmentLibrary::ToEquippableSlot(GetTargetSlot());
}

void UBH_WeaponItem::Activated_Implementation()
{
	// Narrative reads EquippableSlot while equipping: make sure it matches this instance's assignment (clients too).
	RefreshEquippableSlot();
	Super::Activated_Implementation();
}

bool UBH_WeaponItem::CanUse_Implementation() const
{
	if (!Super::CanUse_Implementation())
	{
		return false;
	}
	if (bActive)
	{
		return true; // taking a weapon off is always allowed
	}

	// Putting it on: respect the two-handed lock of the slot we would occupy.
	const UBH_LoadoutComponent* Loadout = UBH_LoadoutComponent::FindLoadoutComponent(GetOwningPawn());
	FText Reason;
	return !Loadout || Loadout->ValidateEquip(this, GetTargetSlot(), Reason);
}

FString UBH_WeaponItem::DescribeBonuses() const
{
	return FString::Printf(TEXT("AP%+.0f DEF%+.0f MaxSTA%+.0f"), AttackPowerBonus, DefenseBonus, MaxStaminaBonus);
}

FString UBH_WeaponItem::GetFriendlyName() const
{
	if (!WeaponName.IsEmpty())
	{
		return WeaponName.ToString();
	}
	if (!DisplayName.IsEmpty())
	{
		return DisplayName.ToString();
	}
	return GetClass()->GetName();
}
