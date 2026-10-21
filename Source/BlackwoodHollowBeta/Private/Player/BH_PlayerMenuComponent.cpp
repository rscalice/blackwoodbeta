// Blackwood Hollow - Player Menu server actions (Phase 13G-0, implementation)

#include "Player/BH_PlayerMenuComponent.h"
#include "Player/BH_PlayerState.h"
#include "Items/BH_ArmorItem.h"
#include "Items/BH_WeaponItem.h"
#include "Combat/BH_LoadoutComponent.h"
#include "Loot/BH_WorldPickup.h"
#include "UI/BH_PlayerMenuLibrary.h"
#include "InventoryComponent.h"
#include "NarrativeItem.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

DEFINE_LOG_CATEGORY_STATIC(LogBHPlayerMenu, Log, All);

UBH_PlayerMenuComponent::UBH_PlayerMenuComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

UBH_PlayerMenuComponent* UBH_PlayerMenuComponent::FindPlayerMenuComponent(const AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}
	if (const APawn* AsPawn = Cast<APawn>(Actor))
	{
		return AsPawn->GetPlayerState() ? AsPawn->GetPlayerState()->FindComponentByClass<UBH_PlayerMenuComponent>() : nullptr;
	}
	if (const AController* AsController = Cast<AController>(Actor))
	{
		return AsController->PlayerState ? AsController->PlayerState->FindComponentByClass<UBH_PlayerMenuComponent>() : nullptr;
	}
	return Actor->FindComponentByClass<UBH_PlayerMenuComponent>();
}

ABH_PlayerState* UBH_PlayerMenuComponent::GetPlayerState() const
{
	return Cast<ABH_PlayerState>(GetOwner());
}

// ---------------------------------------------------------------------------
// Requests (any machine)
// ---------------------------------------------------------------------------

void UBH_PlayerMenuComponent::RequestEquipItem(UNarrativeItem* Item, EBH_EquipSlot Slot)
{
	if (GetOwnerRole() == ROLE_Authority)
	{
		FText Reason;
		const bool bOk = ExecuteEquip(Item, Slot, Reason);
		ReportResult(bOk, Reason);
	}
	else
	{
		ServerEquipItem(Item, Slot);
	}
}

void UBH_PlayerMenuComponent::RequestUnequipSlot(EBH_EquipSlot Slot)
{
	if (GetOwnerRole() == ROLE_Authority)
	{
		FText Reason;
		const bool bOk = ExecuteUnequip(Slot, Reason);
		ReportResult(bOk, Reason);
	}
	else
	{
		ServerUnequipSlot(Slot);
	}
}

void UBH_PlayerMenuComponent::RequestDropItem(UNarrativeItem* Item, int32 Count)
{
	if (GetOwnerRole() == ROLE_Authority)
	{
		FText Reason;
		const bool bOk = ExecuteDrop(Item, Count, Reason);
		ReportResult(bOk, Reason);
	}
	else
	{
		ServerDropItem(Item, Count);
	}
}

// ---------------------------------------------------------------------------
// Server RPCs
// ---------------------------------------------------------------------------

bool UBH_PlayerMenuComponent::ServerEquipItem_Validate(UNarrativeItem* Item, EBH_EquipSlot Slot)
{
	return static_cast<uint8>(Slot) <= static_cast<uint8>(EBH_EquipSlot::Weapon_Off_B);
}

void UBH_PlayerMenuComponent::ServerEquipItem_Implementation(UNarrativeItem* Item, EBH_EquipSlot Slot)
{
	FText Reason;
	const bool bOk = ExecuteEquip(Item, Slot, Reason);
	ReportResult(bOk, Reason);
}

bool UBH_PlayerMenuComponent::ServerUnequipSlot_Validate(EBH_EquipSlot Slot)
{
	return static_cast<uint8>(Slot) <= static_cast<uint8>(EBH_EquipSlot::Weapon_Off_B);
}

void UBH_PlayerMenuComponent::ServerUnequipSlot_Implementation(EBH_EquipSlot Slot)
{
	FText Reason;
	const bool bOk = ExecuteUnequip(Slot, Reason);
	ReportResult(bOk, Reason);
}

bool UBH_PlayerMenuComponent::ServerDropItem_Validate(UNarrativeItem* Item, int32 Count)
{
	return Count > 0 && Count <= MaxDropCount;
}

void UBH_PlayerMenuComponent::ServerDropItem_Implementation(UNarrativeItem* Item, int32 Count)
{
	FText Reason;
	const bool bOk = ExecuteDrop(Item, Count, Reason);
	ReportResult(bOk, Reason);
}

void UBH_PlayerMenuComponent::ClientActionResult_Implementation(bool bSuccess, const FText& Reason)
{
	OnActionResult.Broadcast(bSuccess, Reason);
}

void UBH_PlayerMenuComponent::ReportResult(bool bSuccess, const FText& Reason)
{
	if (!bSuccess)
	{
		UE_LOG(LogBHPlayerMenu, Log, TEXT("Menu action refused for %s: %s"), *GetNameSafe(GetOwner()), *Reason.ToString());
	}
	const ABH_PlayerState* PS = GetPlayerState();
	const APlayerController* PC = PS ? PS->GetPlayerController() : nullptr;
	if (!PC)
	{
		return;
	}
	if (PC->IsLocalController())
	{
		OnActionResult.Broadcast(bSuccess, Reason);
	}
	else
	{
		ClientActionResult(bSuccess, Reason);
	}
}

// ---------------------------------------------------------------------------
// Server logic
// ---------------------------------------------------------------------------

bool UBH_PlayerMenuComponent::ExecuteEquip(UNarrativeItem* Item, EBH_EquipSlot Slot, FText& OutReason)
{
	ABH_PlayerState* PS = GetPlayerState();
	UNarrativeInventoryComponent* Inventory = PS ? PS->GetInventory() : nullptr;
	APawn* PlayerPawn = PS ? PS->GetPawn() : nullptr;
	if (!Inventory || !PlayerPawn)
	{
		OutReason = NSLOCTEXT("BHPlayerMenu", "EquipNoPawn", "You can't change equipment right now.");
		return false;
	}
	if (!Item || Item->OwningInventory != Inventory)
	{
		OutReason = NSLOCTEXT("BHPlayerMenu", "EquipNotInBag", "That item is not in your bag.");
		return false;
	}
	if (!UBH_PlayerMenuLibrary::IsEquipSlotAllowed(PS->IsWeaponSetBUnlocked(), Slot, OutReason))
	{
		return false;
	}

	if (UBH_WeaponItem* Weapon = Cast<UBH_WeaponItem>(Item))
	{
		// The menu asks for the first empty compatible slot, which may be an off hand locked by a two-handed main weapon
		// (Greatsword equipped first -> Longsword / Kite Shield "to Off A" were refused). Resolve to the slot the player means.
		const EBH_EquipSlot ResolvedSlot = UBH_PlayerMenuLibrary::ResolveWeaponEquipSlot(PlayerPawn, Weapon, Slot);
		if (ResolvedSlot != Slot)
		{
			if (!UBH_PlayerMenuLibrary::IsEquipSlotAllowed(PS->IsWeaponSetBUnlocked(), ResolvedSlot, OutReason))
			{
				return false;
			}
			const bool bOpenSet = UBH_EquipmentLibrary::GetSlotLoadoutSet(ResolvedSlot) != UBH_EquipmentLibrary::GetSlotLoadoutSet(Slot);
			UE_LOG(LogBHPlayerMenu, Log, TEXT("Equip %s: %s redirected to %s (%s)"), *Weapon->GetFriendlyName(),
				*UBH_PlayerMenuLibrary::GetEquipSlotDisplayName(Slot).ToString(), *UBH_PlayerMenuLibrary::GetEquipSlotDisplayName(ResolvedSlot).ToString(),
				bOpenSet ? TEXT("off hand locked by a two-hander; open weapon set used") : TEXT("off hand locked by a two-hander; no open set, replacing it"));
		}
		return UBH_EquipmentLibrary::EquipWeaponToSlot(PlayerPawn, Weapon, ResolvedSlot, OutReason);
	}

	if (UBH_ArmorItem* Armor = Cast<UBH_ArmorItem>(Item))
	{
		EBH_EquipSlot ArmorSlot = EBH_EquipSlot::Chest;
		if (!Armor->GetArmorSlot(ArmorSlot) || ArmorSlot != Slot)
		{
			OutReason = FText::Format(NSLOCTEXT("BHPlayerMenu", "EquipWrongSlot", "That piece goes in the {0} slot."), UBH_PlayerMenuLibrary::GetEquipSlotDisplayName(ArmorSlot));
			return false;
		}
		if (!Armor->bActive)
		{
			if (!Armor->CanUse())
			{
				OutReason = NSLOCTEXT("BHPlayerMenu", "EquipCannotUse", "You can't equip that right now.");
				return false;
			}
			// Narrative's equipment component deactivates the piece that was in this slot, and the loadout component applies the stat mods.
			Armor->SetActive(true);
			if (UBH_LoadoutComponent* Loadout = UBH_LoadoutComponent::FindLoadoutComponent(PlayerPawn))
			{
				Loadout->RefreshStatMods();
			}
			PS->ForceNetUpdate();
		}
		OutReason = FText::GetEmpty();
		return true;
	}

	OutReason = NSLOCTEXT("BHPlayerMenu", "EquipNotEquippable", "That can't be equipped.");
	return false;
}

bool UBH_PlayerMenuComponent::ExecuteUnequip(EBH_EquipSlot Slot, FText& OutReason)
{
	ABH_PlayerState* PS = GetPlayerState();
	UNarrativeInventoryComponent* Inventory = PS ? PS->GetInventory() : nullptr;
	APawn* PlayerPawn = PS ? PS->GetPawn() : nullptr;
	if (!Inventory || !PlayerPawn)
	{
		OutReason = NSLOCTEXT("BHPlayerMenu", "UnequipNoPawn", "You can't change equipment right now.");
		return false;
	}

	if (UBH_EquipmentLibrary::IsWeaponSlot(Slot))
	{
		return UBH_EquipmentLibrary::UnequipWeaponSlot(PlayerPawn, Slot, OutReason);
	}

	bool bFound = false;
	for (UNarrativeItem* Candidate : Inventory->GetItems())
	{
		UBH_ArmorItem* Armor = Cast<UBH_ArmorItem>(Candidate);
		EBH_EquipSlot ArmorSlot = EBH_EquipSlot::Chest;
		if (Armor && Armor->bActive && Armor->GetArmorSlot(ArmorSlot) && ArmorSlot == Slot)
		{
			Armor->SetActive(false);
			bFound = true;
		}
	}
	if (!bFound)
	{
		OutReason = NSLOCTEXT("BHPlayerMenu", "UnequipEmpty", "Nothing is equipped there.");
		return false;
	}
	if (UBH_LoadoutComponent* Loadout = UBH_LoadoutComponent::FindLoadoutComponent(PlayerPawn))
	{
		Loadout->RefreshStatMods();
	}
	PS->ForceNetUpdate();
	OutReason = FText::GetEmpty();
	return true;
}

bool UBH_PlayerMenuComponent::ExecuteDrop(UNarrativeItem* Item, int32 Count, FText& OutReason)
{
	ABH_PlayerState* PS = GetPlayerState();
	UNarrativeInventoryComponent* Inventory = PS ? PS->GetInventory() : nullptr;
	APawn* PlayerPawn = PS ? PS->GetPawn() : nullptr;
	if (!Inventory || !PlayerPawn)
	{
		OutReason = NSLOCTEXT("BHPlayerMenu", "DropNoPawn", "You can't drop items right now.");
		return false;
	}
	if (!Item || Item->OwningInventory != Inventory)
	{
		OutReason = NSLOCTEXT("BHPlayerMenu", "DropNotInBag", "That item is not in your bag.");
		return false;
	}
	if (!UBH_PlayerMenuLibrary::CanDropItem(Item, OutReason))
	{
		return false;
	}

	const TSubclassOf<UNarrativeItem> ItemClass = Item->GetClass();
	const int32 DropCount = FMath::Clamp(Count, 1, FMath::Max(1, Item->GetQuantity()));

	FVector DropLocation = PlayerPawn->GetActorLocation() + PlayerPawn->GetActorForwardVector() * DropDistance;
	if (const ACharacter* PlayerCharacter = Cast<ACharacter>(PlayerPawn))
	{
		if (const UCapsuleComponent* Capsule = PlayerCharacter->GetCapsuleComponent())
		{
			DropLocation.Z -= FMath::Max(0.f, Capsule->GetScaledCapsuleHalfHeight() - 20.f); // near the feet
		}
	}

	// Spawn first, remove second: if the spawn fails the player keeps the item.
	ABH_WorldPickup* Pickup = ABH_WorldPickup::SpawnDroppedItem(GetWorld(), ItemClass, DropCount, DropLocation);
	if (!Pickup)
	{
		OutReason = NSLOCTEXT("BHPlayerMenu", "DropSpawnFailed", "There is no room to drop that here.");
		return false;
	}
	if (Inventory->ConsumeItem(Item, DropCount) <= 0)
	{
		Pickup->Destroy();
		OutReason = NSLOCTEXT("BHPlayerMenu", "DropRemoveFailed", "You can't drop this item.");
		return false;
	}
	OutReason = FText::GetEmpty();
	return true;
}
