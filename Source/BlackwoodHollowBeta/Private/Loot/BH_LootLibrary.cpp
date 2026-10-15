// Blackwood Hollow - server-side loot granting helpers (implementation)

#include "Loot/BH_LootLibrary.h"
#include "Hints/BH_HintLibrary.h"
#include "Interaction/BH_InteractorComponent.h"
#include "Combat/BH_LoadoutComponent.h"
#include "Items/BH_WeaponItem.h"
#include "Player/BH_PartyLibrary.h"
#include "Player/BH_PlayerDeathComponent.h"
#include "Player/BH_PlayerState.h"
#include "Progression/BH_RPGSettings.h"
#include "InventoryComponent.h"
#include "NarrativeItem.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Misc/PackageName.h"
#include "GameFramework/Actor.h"
#include "GameFramework/OnlineReplStructs.h"

DEFINE_LOG_CATEGORY_STATIC(LogBHLoot, Log, All);

int32 UBH_LootLibrary::GrantItem(APlayerState* PlayerState, TSubclassOf<UNarrativeItem> ItemClass, int32 Quantity, bool bNotifyPlayer)
{
	if (!PlayerState || !PlayerState->HasAuthority() || !ItemClass || Quantity <= 0)
	{
		return 0;
	}
	// The inventory lives on the PlayerState (survives respawns, replicates to the owner). Other PlayerState classes have none.
	const ABH_PlayerState* BHPlayerState = Cast<ABH_PlayerState>(PlayerState);
	UNarrativeInventoryComponent* Inventory = BHPlayerState ? BHPlayerState->GetInventory() : nullptr;
	if (!Inventory)
	{
		UE_LOG(LogBHLoot, Warning, TEXT("GrantItem: %s has no Narrative inventory (is the game mode using PS_BlackwoodHollow?)."), *GetNameSafe(PlayerState));
		return 0;
	}

	const FItemAddResult Result = Inventory->TryAddItemFromClass(ItemClass, Quantity, /*bCheckAutoUse*/ true);
	if (bNotifyPlayer)
	{
		if (UBH_InteractorComponent* Interactor = UBH_InteractorComponent::Find(PlayerState->GetPawn()))
		{
			if (Result.AmountGiven > 0)
			{
				Interactor->NotifyItemGranted(GetItemDisplayName(ItemClass), Result.AmountGiven);
			}
			if (Result.AmountGiven < Quantity)
			{
				Interactor->NotifyInventoryFull(GetItemDisplayName(ItemClass)); // 13C (#14): "Inventory full" through the queue
			}
		}
	}
	return Result.AmountGiven;
}

int32 UBH_LootLibrary::GrantItemToPawn(APawn* Pawn, TSubclassOf<UNarrativeItem> ItemClass, int32 Quantity, bool bNotifyPlayer)
{
	return Pawn ? GrantItem(Pawn->GetPlayerState(), ItemClass, Quantity, bNotifyPlayer) : 0;
}

namespace BH_LootLibrary_Private
{
	/**
	 * Phase 13-0b. Equips the weapon item just granted (exact class match, first one not already active) into Slot on PlayerState's pawn loadout.
	 * The inventory lives on the PlayerState but the loadout / equipment components live on the pawn. A dead, downed or ragdolled player keeps
	 * the same pawn (revive and hub respawn teleport it, they never replace it), so equipping works for them too. Only a player with no pawn at
	 * all (mid-join / disconnecting) is skipped: the item then stays in the inventory and can be equipped by hand.
	 */
	static void AutoEquipGrantedItem(APlayerState* PlayerState, TSubclassOf<UNarrativeItem> ItemClass, EBH_EquipSlot Slot)
	{
		const ABH_PlayerState* BHPlayerState = Cast<ABH_PlayerState>(PlayerState);
		UNarrativeInventoryComponent* Inventory = BHPlayerState ? BHPlayerState->GetInventory() : nullptr;
		UBH_LoadoutComponent* Loadout = UBH_LoadoutComponent::FindLoadoutComponent(PlayerState ? PlayerState->GetPawn() : nullptr);
		if (!Inventory || !Loadout)
		{
			UE_LOG(LogBHLoot, Warning, TEXT("AutoEquip: %s has no pawn loadout yet - %s stays in the inventory."), *GetNameSafe(PlayerState), *GetNameSafe(ItemClass.Get()));
			return;
		}

		UBH_WeaponItem* Weapon = nullptr;
		for (UNarrativeItem* Item : Inventory->GetItems())
		{
			UBH_WeaponItem* Candidate = Cast<UBH_WeaponItem>(Item);
			if (Candidate && Candidate->GetClass() == ItemClass.Get() && !Candidate->bActive)
			{
				Weapon = Candidate;
				break;
			}
		}
		if (!Weapon)
		{
			return; // every copy is already equipped somewhere
		}

		FText Reason;
		if (!Loadout->EquipWeaponToSlot(Weapon, Slot, Reason))
		{
			UE_LOG(LogBHLoot, Warning, TEXT("AutoEquip: %s could not equip %s: %s"), *GetNameSafe(PlayerState), *GetNameSafe(ItemClass.Get()), *Reason.ToString());
		}
	}

	/** Grants one drop line to one player (honouring bOncePerPlayer), then auto-equips it when asked. */
	static void GrantDropToPlayer(APlayerState* PlayerState, const FBH_DropEntry& Entry, TSubclassOf<UNarrativeItem> ItemClass, int32 Quantity)
	{
		if (!PlayerState || !ItemClass)
		{
			return;
		}
		if (Entry.bOncePerPlayer && UBH_LootLibrary::GetItemCount(PlayerState, ItemClass) > 0)
		{
			return;
		}
		const int32 Given = UBH_LootLibrary::GrantItem(PlayerState, ItemClass, Quantity);
		if (Given > 0 && Entry.bAutoEquip)
		{
			AutoEquipGrantedItem(PlayerState, ItemClass, Entry.AutoEquipSlot);
			UBH_HintLibrary::TriggerHintKey(PlayerState, TEXT("Drop.AutoEquip")); // 13E: the kite shield (13-0b)
		}
	}
}

void UBH_LootLibrary::GrantEnemyDrops(const AActor* Source, const FBH_DropTable& DropTable)
{
	if (!Source || !Source->HasAuthority() || DropTable.Entries.IsEmpty())
	{
		return;
	}
	const UWorld* DropWorld = Source->GetWorld();
	const UBH_RPGSettings* Settings = UBH_RPGSettings::Get();
	if (!DropWorld)
	{
		return;
	}

	const float Radius = Settings ? Settings->XPShareRadius : 0.f;
	const double RadiusSq = static_cast<double>(Radius) * static_cast<double>(Radius);
	const FVector Origin = Source->GetActorLocation();

	for (FConstPlayerControllerIterator It = DropWorld->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (!Pawn || !UBH_PlayerDeathComponent::IsLivingPlayer(Pawn))
		{
			continue;
		}
		if (Radius > 0.f && FVector::DistSquared(Pawn->GetActorLocation(), Origin) > RadiusSq)
		{
			continue;
		}

		// Independent roll per entry, per player. Whole-party entries are handled once, below.
		for (const FBH_DropEntry& Entry : DropTable.Entries)
		{
			if (Entry.bGrantToWholeParty)
			{
				continue;
			}
			if (Entry.ItemClass.IsNull() || Entry.Chance <= 0.f || FMath::FRand() >= Entry.Chance)
			{
				continue;
			}
			const int32 MinQty = FMath::Max(1, Entry.MinQuantity);
			const int32 Quantity = FMath::RandRange(MinQty, FMath::Max(MinQty, Entry.MaxQuantity));
			UClass* LoadedClass = Entry.ItemClass.LoadSynchronous();
			if (LoadedClass)
			{
				BH_LootLibrary_Private::GrantDropToPlayer(Pawn->GetPlayerState(), Entry, LoadedClass, Quantity);
			}
		}
	}

	// Phase 13-0b: whole-party entries. One roll per entry for the whole party; every member (any distance, alive or dead) gets the result.
	for (const FBH_DropEntry& Entry : DropTable.Entries)
	{
		if (!Entry.bGrantToWholeParty || Entry.ItemClass.IsNull() || Entry.Chance <= 0.f || FMath::FRand() >= Entry.Chance)
		{
			continue;
		}
		const int32 MinQty = FMath::Max(1, Entry.MinQuantity);
		const int32 Quantity = FMath::RandRange(MinQty, FMath::Max(MinQty, Entry.MaxQuantity));
		UClass* LoadedClass = Entry.ItemClass.LoadSynchronous();
		if (!LoadedClass)
		{
			continue;
		}
		UBH_PartyLibrary::ForEachPartyMember(Source, [&Entry, LoadedClass, Quantity](APlayerState& MemberState, APawn*)
		{
			BH_LootLibrary_Private::GrantDropToPlayer(&MemberState, Entry, LoadedClass, Quantity);
		});
	}
}

int32 UBH_LootLibrary::GetItemCount(const APlayerState* PlayerState, TSubclassOf<UNarrativeItem> ItemClass)
{
	const ABH_PlayerState* BHPlayerState = Cast<ABH_PlayerState>(PlayerState);
	const UNarrativeInventoryComponent* Inventory = (BHPlayerState && ItemClass) ? BHPlayerState->GetInventory() : nullptr;
	if (!Inventory)
	{
		return 0;
	}
	int32 Total = 0;
	for (const UNarrativeItem* Item : Inventory->GetItems())
	{
		if (Item && Item->GetClass() == ItemClass.Get())
		{
			Total += FMath::Max(Item->GetQuantity(), 0);
		}
	}
	return Total;
}

int32 UBH_LootLibrary::RemoveItemFromPlayer(APlayerState* PlayerState, TSubclassOf<UNarrativeItem> ItemClass, int32 Quantity)
{
	if (!PlayerState || !PlayerState->HasAuthority() || !ItemClass || Quantity <= 0)
	{
		return 0;
	}
	const ABH_PlayerState* BHPlayerState = Cast<ABH_PlayerState>(PlayerState);
	UNarrativeInventoryComponent* Inventory = BHPlayerState ? BHPlayerState->GetInventory() : nullptr;
	if (!Inventory)
	{
		return 0;
	}

	int32 Removed = 0;
	// GetItems() returns a copy: safe to consume (and so remove) entries while walking it. Exact class match, like GetItemCount.
	for (UNarrativeItem* Item : Inventory->GetItems())
	{
		if (Removed >= Quantity)
		{
			break;
		}
		if (Item && Item->GetClass() == ItemClass.Get())
		{
			Removed += Inventory->ConsumeItem(Item, Quantity - Removed);
		}
	}
	return Removed;
}

FString UBH_LootLibrary::GetPlayerKey(const APlayerState* PlayerState)
{
	if (!PlayerState)
	{
		return FString();
	}
	const FUniqueNetIdRepl& NetId = PlayerState->GetUniqueId();
	if (NetId.IsValid())
	{
		return NetId.ToString();
	}
	// Editor / PIE / no online subsystem: the unique id is invalid. The name is not stable across a rejoin, but is stable for a session.
	if (!PlayerState->GetPlayerName().IsEmpty())
	{
		return PlayerState->GetPlayerName();
	}
	return FString::Printf(TEXT("PlayerId_%d"), PlayerState->GetPlayerId());
}

TSubclassOf<UNarrativeItem> UBH_LootLibrary::ResolveItemClass(const FString& ItemName)
{
	FString Path = ItemName.TrimStartAndEnd();
	if (Path.Equals(TEXT("shard"), ESearchCase::IgnoreCase) || Path.Equals(TEXT("coral"), ESearchCase::IgnoreCase))
	{
		Path = BH_LootPaths::CorruptedCoralShard;
	}
	else if (Path.Equals(TEXT("sap"), ESearchCase::IgnoreCase) || Path.Equals(TEXT("heartwood"), ESearchCase::IgnoreCase))
	{
		Path = BH_LootPaths::HeartwoodSap;
	}
	else if (Path.Equals(TEXT("incense"), ESearchCase::IgnoreCase))
	{
		Path = BH_LootPaths::WardensIncense;
	}
	else if (!Path.Contains(TEXT("/")))
	{
		return nullptr;
	}
	else if (!Path.Contains(TEXT(".")))
	{
		// "/Game/.../BI_Foo" -> "/Game/.../BI_Foo.BI_Foo_C"
		const FString ShortName = FPackageName::GetShortName(Path);
		Path = FString::Printf(TEXT("%s.%s_C"), *Path, *ShortName);
	}

	UClass* Loaded = LoadClass<UNarrativeItem>(nullptr, *Path);
	return Loaded;
}

FText UBH_LootLibrary::GetItemDisplayName(TSubclassOf<UNarrativeItem> ItemClass)
{
	if (!ItemClass)
	{
		return FText::GetEmpty();
	}
	const UNarrativeItem* CDO = ItemClass->GetDefaultObject<UNarrativeItem>();
	if (CDO && !CDO->DisplayName.IsEmpty())
	{
		return CDO->DisplayName;
	}
	return FText::FromString(ItemClass->GetName());
}
