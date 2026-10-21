// Blackwood Hollow - Player Menu helpers (Phase 13G-0, implementation)

#include "UI/BH_PlayerMenuLibrary.h"
#include "UI/BH_ItemIconSettings.h"
#include "Items/BH_ArmorItem.h"
#include "Items/BH_WeaponItem.h"
#include "Combat/BH_LoadoutComponent.h"
#include "Components/BPC_HeartFragment.h"
#include "Player/BH_PlayerState.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystem/StatusEffects/BH_FractureEffects.h"
#include "AbilitySystemComponent.h"
#include "InventoryComponent.h"
#include "NarrativeItem.h"
#include "NativeGameplayTags.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_BH_PlayerMenu_ItemQuest, "Item.Quest");

namespace BH_PlayerMenuLibrary_Private
{
	static void AddBonuses(FBH_EquipStatBonuses& Sum, const UNarrativeItem* Item)
	{
		FBH_EquipStatBonuses Bonuses;
		if (UBH_PlayerMenuLibrary::GetItemBonuses(Item, Bonuses))
		{
			Sum.AttackPower += Bonuses.AttackPower;
			Sum.Defense += Bonuses.Defense;
			Sum.MaxStamina += Bonuses.MaxStamina;
		}
	}

	static float ReadAttribute(const UAbilitySystemComponent* ASC, const FGameplayAttribute& Attribute)
	{
		return ASC ? ASC->GetNumericAttribute(Attribute) : 0.f;
	}

	static FBH_ItemStatDelta MakeRow(FName StatId, const FText& Label, float Current, float Replaced, float Incoming)
	{
		FBH_ItemStatDelta Row;
		Row.StatId = StatId;
		Row.Label = Label;
		Row.Current = Current;
		Row.AfterEquip = Current - Replaced + Incoming;
		Row.Delta = Row.AfterEquip - Row.Current;
		return Row;
	}
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

FText UBH_PlayerMenuLibrary::GetEquipSlotDisplayName(EBH_EquipSlot Slot)
{
	switch (Slot)
	{
	case EBH_EquipSlot::Head:          return NSLOCTEXT("BHPlayerMenu", "SlotHead", "Helm");
	case EBH_EquipSlot::Chest:         return NSLOCTEXT("BHPlayerMenu", "SlotChest", "Chest");
	case EBH_EquipSlot::Arms:          return NSLOCTEXT("BHPlayerMenu", "SlotArms", "Gloves");
	case EBH_EquipSlot::Legs:          return NSLOCTEXT("BHPlayerMenu", "SlotLegs", "Pants");
	case EBH_EquipSlot::Feet:          return NSLOCTEXT("BHPlayerMenu", "SlotFeet", "Boots");
	case EBH_EquipSlot::Weapon_Main_A: return NSLOCTEXT("BHPlayerMenu", "SlotMainA", "Main Hand A");
	case EBH_EquipSlot::Weapon_Off_A:  return NSLOCTEXT("BHPlayerMenu", "SlotOffA", "Off Hand A");
	case EBH_EquipSlot::Weapon_Main_B: return NSLOCTEXT("BHPlayerMenu", "SlotMainB", "Main Hand B");
	case EBH_EquipSlot::Weapon_Off_B:  return NSLOCTEXT("BHPlayerMenu", "SlotOffB", "Off Hand B");
	default:                           return FText::GetEmpty();
	}
}

bool UBH_PlayerMenuLibrary::IsEquipSlotAllowed(bool bSetBUnlocked, EBH_EquipSlot Slot, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	if (UBH_EquipmentLibrary::IsWeaponSlot(Slot) && UBH_EquipmentLibrary::GetSlotLoadoutSet(Slot) == EBH_LoadoutSet::B && !bSetBUnlocked)
	{
		OutReason = NSLOCTEXT("BHPlayerMenu", "SetBLocked", "Weapon set B unlocks on Island 2.");
		return false;
	}
	return true;
}

bool UBH_PlayerMenuLibrary::CanEquipToSlot(const ABH_PlayerState* PlayerState, EBH_EquipSlot Slot, FText& OutReason)
{
	return IsEquipSlotAllowed(PlayerState && PlayerState->IsWeaponSetBUnlocked(), Slot, OutReason);
}

// ---------------------------------------------------------------------------
// Items
// ---------------------------------------------------------------------------

bool UBH_PlayerMenuLibrary::IsItemEquipped(const UNarrativeItem* Item)
{
	return Item && Item->bCanActivate && Item->bActive;
}

FGameplayTag UBH_PlayerMenuLibrary::GetQuestItemTag()
{
	return TAG_BH_PlayerMenu_ItemQuest.GetTag();
}

bool UBH_PlayerMenuLibrary::CanDropItem(const UNarrativeItem* Item, FText& OutReason)
{
	OutReason = FText::GetEmpty();
	if (!Item)
	{
		OutReason = NSLOCTEXT("BHPlayerMenu", "DropNoItem", "Nothing to drop.");
		return false;
	}
	if (IsItemEquipped(Item))
	{
		OutReason = NSLOCTEXT("BHPlayerMenu", "DropEquipped", "Unequip it before dropping it.");
		return false;
	}
	if (Item->ItemTags.HasTag(GetQuestItemTag()) || !Item->CanBeRemoved())
	{
		OutReason = NSLOCTEXT("BHPlayerMenu", "DropQuest", "You can't drop this item.");
		return false;
	}
	return true;
}

EBH_ItemCategory UBH_PlayerMenuLibrary::GetItemCategory(TSubclassOf<UNarrativeItem> ItemClass)
{
	if (!ItemClass)
	{
		return EBH_ItemCategory::Other;
	}
	if (ItemClass->IsChildOf(UBH_WeaponItem::StaticClass()))
	{
		return EBH_ItemCategory::Weapon;
	}
	if (ItemClass->IsChildOf(UBH_ArmorItem::StaticClass()))
	{
		return EBH_ItemCategory::Armor;
	}
	const UNarrativeItem* CDO = ItemClass->GetDefaultObject<UNarrativeItem>();
	return (CDO && CDO->bConsumeOnUse) ? EBH_ItemCategory::Consumable : EBH_ItemCategory::Other;
}

UTexture2D* UBH_PlayerMenuLibrary::GetItemIcon(TSubclassOf<UNarrativeItem> ItemClass, UTexture2D* WeaponFallback, UTexture2D* ArmorFallback, UTexture2D* ConsumableFallback, UTexture2D* OtherFallback)
{
	if (ItemClass)
	{
		if (const UNarrativeItem* CDO = ItemClass->GetDefaultObject<UNarrativeItem>())
		{
			if (!CDO->Thumbnail.IsNull())
			{
				if (UTexture2D* Icon = CDO->Thumbnail.LoadSynchronous())
				{
					return Icon;
				}
			}
		}
	}
	// Issue #122: a pin left empty falls back to the project-wide default for the category (UBH_ItemIconSettings).
	const UBH_ItemIconSettings* IconSettings = UBH_ItemIconSettings::Get();
	UTexture2D* PinFallback = nullptr;
	const TSoftObjectPtr<UTexture2D>* DefaultFallback = nullptr;
	switch (GetItemCategory(ItemClass))
	{
	case EBH_ItemCategory::Weapon:     PinFallback = WeaponFallback;     DefaultFallback = &IconSettings->WeaponFallback; break;
	case EBH_ItemCategory::Armor:      PinFallback = ArmorFallback;      DefaultFallback = &IconSettings->ArmorFallback; break;
	case EBH_ItemCategory::Consumable: PinFallback = ConsumableFallback; DefaultFallback = &IconSettings->ConsumableFallback; break;
	default:                           PinFallback = OtherFallback;      DefaultFallback = &IconSettings->OtherFallback; break;
	}
	if (PinFallback)
	{
		return PinFallback;
	}
	return DefaultFallback && !DefaultFallback->IsNull() ? DefaultFallback->LoadSynchronous() : nullptr;
}

UTexture2D* UBH_PlayerMenuLibrary::GetItemIconDefault(TSubclassOf<UNarrativeItem> ItemClass)
{
	return GetItemIcon(ItemClass, nullptr, nullptr, nullptr, nullptr);
}

bool UBH_PlayerMenuLibrary::GetItemBonuses(const UNarrativeItem* Item, FBH_EquipStatBonuses& OutBonuses)
{
	OutBonuses = FBH_EquipStatBonuses();
	if (const UBH_WeaponItem* Weapon = Cast<UBH_WeaponItem>(Item))
	{
		OutBonuses.AttackPower = Weapon->AttackPowerBonus;
		OutBonuses.Defense = Weapon->DefenseBonus;
		OutBonuses.MaxStamina = Weapon->MaxStaminaBonus;
		return true;
	}
	if (const UBH_ArmorItem* Armor = Cast<UBH_ArmorItem>(Item))
	{
		OutBonuses.AttackPower = Armor->AttackPowerBonus;
		OutBonuses.Defense = Armor->DefenseBonus;
		OutBonuses.MaxStamina = Armor->MaxStaminaBonus;
		return true;
	}
	return false;
}

void UBH_PlayerMenuLibrary::BuildStatDeltaRows(const FBH_EquipStatBonuses& CurrentTotals, const FBH_EquipStatBonuses& Replaced, const FBH_EquipStatBonuses& Incoming, TArray<FBH_ItemStatDelta>& OutRows)
{
	using namespace BH_PlayerMenuLibrary_Private;
	OutRows.Reset();
	OutRows.Add(MakeRow(FName(TEXT("AttackPower")), NSLOCTEXT("BHPlayerMenu", "StatAttackPower", "Attack Power"), CurrentTotals.AttackPower, Replaced.AttackPower, Incoming.AttackPower));
	OutRows.Add(MakeRow(FName(TEXT("Defense")), NSLOCTEXT("BHPlayerMenu", "StatDefense", "Defense"), CurrentTotals.Defense, Replaced.Defense, Incoming.Defense));
	OutRows.Add(MakeRow(FName(TEXT("MaxStamina")), NSLOCTEXT("BHPlayerMenu", "StatMaxStamina", "Max Stamina"), CurrentTotals.MaxStamina, Replaced.MaxStamina, Incoming.MaxStamina));
}

bool UBH_PlayerMenuLibrary::GetItemStatDeltas(APawn* Pawn, UNarrativeItem* Item, EBH_EquipSlot TargetSlot, TArray<FBH_ItemStatDelta>& OutRows)
{
	using namespace BH_PlayerMenuLibrary_Private;
	OutRows.Reset();

	FBH_EquipStatBonuses Incoming;
	if (!Pawn || !GetItemBonuses(Item, Incoming))
	{
		return false;
	}

	FBH_EquipStatBonuses Replaced;
	UBH_LoadoutComponent* Loadout = UBH_LoadoutComponent::FindLoadoutComponent(Pawn);

	if (const UBH_WeaponItem* Weapon = Cast<UBH_WeaponItem>(Item))
	{
		if (!UBH_EquipmentLibrary::IsWeaponSlot(TargetSlot) || !Loadout)
		{
			return false;
		}
		const EBH_LoadoutSet Set = UBH_EquipmentLibrary::GetSlotLoadoutSet(TargetSlot);
		EBH_LoadoutSet ActiveSet = EBH_LoadoutSet::A;
		const bool bCounts = !Loadout->GetActiveLoadoutSet(ActiveSet) || ActiveSet == Set;
		if (bCounts)
		{
			const UBH_WeaponItem* Occupant = Loadout->GetItemInSlot(TargetSlot);
			if (Occupant && Occupant != Weapon)
			{
				AddBonuses(Replaced, Occupant);
			}
			if (Weapon->GripType == EBH_WeaponGripType::TwoHanded)
			{
				const UBH_WeaponItem* OffHand = Loadout->GetItemInSlot(UBH_EquipmentLibrary::GetOffSlot(Set));
				if (OffHand && OffHand != Weapon)
				{
					AddBonuses(Replaced, OffHand);
				}
			}
			if (Weapon->bActive)
			{
				Incoming = FBH_EquipStatBonuses(); // already counted in the current totals
			}
		}
		else
		{
			Incoming = FBH_EquipStatBonuses(); // the other set: the active stance (and so the stat mods) do not change
		}
	}
	else if (const UBH_ArmorItem* Armor = Cast<UBH_ArmorItem>(Item))
	{
		if (const APlayerState* PS = Pawn->GetPlayerState())
		{
			if (const UNarrativeInventoryComponent* Inventory = PS->FindComponentByClass<UNarrativeInventoryComponent>())
			{
				EBH_EquipSlot ArmorSlot = EBH_EquipSlot::Chest;
				Armor->GetArmorSlot(ArmorSlot);
				for (const UNarrativeItem* Other : Inventory->GetItems())
				{
					const UBH_ArmorItem* OtherArmor = Cast<UBH_ArmorItem>(Other);
					EBH_EquipSlot OtherSlot = EBH_EquipSlot::Chest;
					if (OtherArmor && OtherArmor != Armor && OtherArmor->bActive && OtherArmor->GetArmorSlot(OtherSlot) && OtherSlot == ArmorSlot)
					{
						AddBonuses(Replaced, OtherArmor);
					}
				}
			}
		}
		if (Armor->bActive)
		{
			Incoming = FBH_EquipStatBonuses();
		}
	}

	FBH_EquipStatBonuses Totals;
	if (const UAbilitySystemComponent* ASC = UBH_FractureLibrary::ResolveASC(Pawn))
	{
		Totals.AttackPower = ReadAttribute(ASC, UAH_AttributeSet::GetAttackPowerAttribute());
		Totals.Defense = ReadAttribute(ASC, UAH_AttributeSet::GetDefenseAttribute());
		Totals.MaxStamina = ReadAttribute(ASC, UAH_AttributeSet::GetMaxStaminaAttribute());
	}
	BuildStatDeltaRows(Totals, Replaced, Incoming, OutRows);
	return true;
}

// ---------------------------------------------------------------------------
// Bag
// ---------------------------------------------------------------------------

bool UBH_PlayerMenuLibrary::GetBagUsage(const ABH_PlayerState* PlayerState, int32& OutUsed, int32& OutCapacity)
{
	OutUsed = 0;
	OutCapacity = 0;
	const UNarrativeInventoryComponent* Inventory = PlayerState ? PlayerState->GetInventory() : nullptr;
	if (!Inventory)
	{
		return false;
	}
	OutUsed = Inventory->GetItems().Num();
	OutCapacity = Inventory->GetCapacity();
	return true;
}

bool UBH_PlayerMenuLibrary::GetBagWeight(const ABH_PlayerState* PlayerState, float& OutWeight, float& OutWeightCapacity)
{
	OutWeight = 0.f;
	OutWeightCapacity = 0.f;
	const UNarrativeInventoryComponent* Inventory = PlayerState ? PlayerState->GetInventory() : nullptr;
	if (!Inventory)
	{
		return false;
	}
	OutWeight = Inventory->GetCurrentWeight();
	OutWeightCapacity = Inventory->GetWeightCapacity();
	return true;
}

// ---------------------------------------------------------------------------
// Heart-Fragment
// ---------------------------------------------------------------------------

float UBH_PlayerMenuLibrary::GetFragmentShielding(const APawn* Pawn)
{
	const UBPC_HeartFragment* Fragment = Pawn ? Pawn->FindComponentByClass<UBPC_HeartFragment>() : nullptr;
	return Fragment ? FMath::Clamp(Fragment->GetShieldingFraction(), 0.f, 1.f) : 0.f;
}
