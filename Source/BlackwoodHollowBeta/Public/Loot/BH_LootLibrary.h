// Blackwood Hollow - server-side loot granting helpers (Phase 11C)
// Target: Unreal Engine 5.8 (C++)
//
// Every item a player receives goes through GrantItem, which puts it into the Narrative inventory on the PLAYER STATE
// (ABH_PlayerState::GetInventory) - never on the pawn - and tells the owning client what arrived (toast hook).
// All grant functions are SERVER ONLY and do nothing on a client.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Loot/BH_LootTypes.h"
#include "BH_LootLibrary.generated.h"

class APawn;
class APlayerState;
class UNarrativeItem;

UCLASS()
class BLACKWOODHOLLOWBETA_API UBH_LootLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** SERVER. Adds Quantity of ItemClass to PlayerState's Narrative inventory. Returns how many actually fit (weight / slots can cut it short). */
	UFUNCTION(BlueprintCallable, Category = "BH|Loot")
	static int32 GrantItem(APlayerState* PlayerState, TSubclassOf<UNarrativeItem> ItemClass, int32 Quantity = 1, bool bNotifyPlayer = true);

	/** SERVER. Same as GrantItem for the player state of Pawn. */
	UFUNCTION(BlueprintCallable, Category = "BH|Loot")
	static int32 GrantItemToPawn(APawn* Pawn, TSubclassOf<UNarrativeItem> ItemClass, int32 Quantity = 1, bool bNotifyPlayer = true);

	/**
	 * SERVER. Enemy drop hook: rolls DropTable once for EVERY living player pawn within UBH_RPGSettings::XPShareRadius of Source (the same
	 * rule as kill XP) and grants the winnings straight into that player's inventory.
	 */
	UFUNCTION(BlueprintCallable, Category = "BH|Loot")
	static void GrantEnemyDrops(const AActor* Source, const FBH_DropTable& DropTable);

	/** A stable per-player key for "has this player already looted X" bookkeeping (unique net id, else name, else player id). */
	UFUNCTION(BlueprintPure, Category = "BH|Loot")
	static FString GetPlayerKey(const APlayerState* PlayerState);

	/** "shard" / "sap" / "incense" aliases (case-insensitive), a full class path, or a package path to an item Blueprint. Null if unknown. */
	UFUNCTION(BlueprintPure, Category = "BH|Loot")
	static TSubclassOf<UNarrativeItem> ResolveItemClass(const FString& ItemName);

	/** Display name of an item class (its CDO's DisplayName), falling back to the class name. */
	static FText GetItemDisplayName(TSubclassOf<UNarrativeItem> ItemClass);
};
