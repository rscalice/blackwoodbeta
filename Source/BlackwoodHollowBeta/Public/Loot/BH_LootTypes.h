// Blackwood Hollow - loot shared types (Phase 11C)
// Target: Unreal Engine 5.8 (C++)
//
// FBH_DropTable is the "enemy drop table hook": a plain chance list on an enemy (ABH_EnemyBase, UBH_CombatIdentityComponent) that
// UBH_LootLibrary::GrantEnemyDrops rolls PER LIVING PLAYER IN XP-SHARE RANGE when the enemy dies (server). Drops go straight into
// each player's Narrative inventory (no world pickup), so there is nothing to race for and nothing to replicate.
// Containers use Narrative's own FLootTableRoll / DataTable instead (see ABH_LootContainer).

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "BH_LootTypes.generated.h"

class UNarrativeItem;

namespace BH_LootPaths
{
	// Item Blueprints created in Phase 11C (parent UNarrativeItem). Soft paths, so C++ has no hard asset dependency.
	inline constexpr const TCHAR* CorruptedCoralShard = TEXT("/Game/BlackwoodHollow/Items/Materials/BI_Material_CorruptedCoralShard.BI_Material_CorruptedCoralShard_C");
	inline constexpr const TCHAR* HeartwoodSap = TEXT("/Game/BlackwoodHollow/Items/Consumables/BI_Consumable_HeartwoodSap.BI_Consumable_HeartwoodSap_C");
	inline constexpr const TCHAR* WardensIncense = TEXT("/Game/BlackwoodHollow/Items/Consumables/BI_Consumable_WardensIncense.BI_Consumable_WardensIncense_C");
}

/** One line of an enemy drop table. */
USTRUCT(BlueprintType)
struct BLACKWOODHOLLOWBETA_API FBH_DropEntry
{
	GENERATED_BODY()

	/** The Narrative item Blueprint to grant. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop")
	TSoftClassPtr<UNarrativeItem> ItemClass;

	/** 0..1 chance, rolled independently per player. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Chance = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop", meta = (ClampMin = "1"))
	int32 MinQuantity = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop", meta = (ClampMin = "1"))
	int32 MaxQuantity = 1;
};

/** Independent-chance list: every entry rolls on its own (a kill can drop several things, or nothing). */
USTRUCT(BlueprintType)
struct BLACKWOODHOLLOWBETA_API FBH_DropTable
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drop")
	TArray<FBH_DropEntry> Entries;
};
