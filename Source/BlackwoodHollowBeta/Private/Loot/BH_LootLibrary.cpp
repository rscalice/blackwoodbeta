// Blackwood Hollow - server-side loot granting helpers (implementation)

#include "Loot/BH_LootLibrary.h"
#include "Interaction/BH_InteractorComponent.h"
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
	if (Result.AmountGiven > 0 && bNotifyPlayer)
	{
		if (UBH_InteractorComponent* Interactor = UBH_InteractorComponent::Find(PlayerState->GetPawn()))
		{
			Interactor->NotifyItemGranted(GetItemDisplayName(ItemClass), Result.AmountGiven);
		}
	}
	return Result.AmountGiven;
}

int32 UBH_LootLibrary::GrantItemToPawn(APawn* Pawn, TSubclassOf<UNarrativeItem> ItemClass, int32 Quantity, bool bNotifyPlayer)
{
	return Pawn ? GrantItem(Pawn->GetPlayerState(), ItemClass, Quantity, bNotifyPlayer) : 0;
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

		// Independent roll per entry, per player.
		for (const FBH_DropEntry& Entry : DropTable.Entries)
		{
			if (Entry.ItemClass.IsNull() || Entry.Chance <= 0.f || FMath::FRand() >= Entry.Chance)
			{
				continue;
			}
			const int32 MinQty = FMath::Max(1, Entry.MinQuantity);
			const int32 Quantity = FMath::RandRange(MinQty, FMath::Max(MinQty, Entry.MaxQuantity));
			UClass* LoadedClass = Entry.ItemClass.LoadSynchronous();
			if (LoadedClass)
			{
				GrantItemToPawn(Pawn, LoadedClass, Quantity);
			}
		}
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
