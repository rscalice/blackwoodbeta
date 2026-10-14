// Blackwood Hollow - notification queue types (Phase 13C)
// Target: Unreal Engine 5.8 (C++), UMG

#pragma once

#include "CoreMinimal.h"
#include "BH_NotificationTypes.generated.h"

/** What a notification is about. The enum order IS the priority: death shows first, loot last. */
UENUM(BlueprintType)
enum class EBH_NotificationKind : uint8
{
	Death,
	LevelUp,
	Quest,
	Discovery,
	Loot,
	/** "Inventory full", refused consumables. Ranks with Loot, never batched. */
	Notice,
	/** "+45 XP". Lowest priority, batched like loot (one running total). */
	Experience,
};

/** Lower number = shows first. Notice ranks with Loot; Experience ranks last. */
inline int32 BH_NotificationPriority(EBH_NotificationKind Kind)
{
	return Kind == EBH_NotificationKind::Notice ? static_cast<int32>(EBH_NotificationKind::Loot) : static_cast<int32>(Kind);
}

/** One batched loot line: "+3 Coral Shard". */
USTRUCT(BlueprintType)
struct BLACKWOODHOLLOWBETA_API FBH_LootLine
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Notification")
	FText ItemName;

	UPROPERTY(BlueprintReadOnly, Category = "Notification")
	int32 Quantity = 0;
};

/** What the queue widget shows. Title and Body may be empty (a loot toast has only a body of "+N Item" lines). */
USTRUCT(BlueprintType)
struct BLACKWOODHOLLOWBETA_API FBH_Notification
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Notification")
	EBH_NotificationKind Kind = EBH_NotificationKind::Notice;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Notification")
	FText Title;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Notification")
	FText Body;

	/** Harm / refusal look (Crimson in the widget). */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Notification")
	bool bWarning = false;

	/** Hold seconds. 0 = the default for the kind from the HUD settings. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Notification", meta = (ClampMin = "0.0"))
	float HoldSeconds = 0.f;

	/** Loot only: the batched lines. Body is rebuilt from them. */
	UPROPERTY(BlueprintReadOnly, Category = "Notification")
	TArray<FBH_LootLine> LootLines;
};
