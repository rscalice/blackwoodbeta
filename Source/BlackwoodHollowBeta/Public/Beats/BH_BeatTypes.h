// Blackwood Hollow - beat data types (Phase 13B+)
// Target: Unreal Engine 5.8 (C++)

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "GameFramework/PlayerState.h"
#include "BH_BeatTypes.generated.h"

class AActor;

/**
 * One row of DT_BH_Beats = one beat of the golden path. Row ORDER is the beat order (the first row is the first beat).
 * Phase 14 quests can replace the table, or write the current beat themselves through UBH_BeatTrackerComponent.
 */
USTRUCT(BlueprintType)
struct BLACKWOODHOLLOWBETA_API FBH_BeatRow : public FTableRowBase
{
	GENERATED_BODY()

	/** Beat.Island1.* tag naming this beat. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Beat", meta = (Categories = "Beat"))
	FGameplayTag BeatId;

	/** One short line for the compass and the quest tracker slot. Never names a key. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Beat")
	FText ObjectiveText;

	/** The ABH_BeatTarget actor to point at: the placed actor whose BeatTag equals this. Empty = use BeatId. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Beat", meta = (Categories = "Beat"))
	FGameplayTag TargetTag;

	/** Optional explicit actor, used before the tag lookup. Only reliable for always-loaded actors (World Partition). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Beat")
	TSoftObjectPtr<AActor> TargetActor;

	/** World flag (ABH_GameState) that must be set before the marker shows, so it never points through a locked gate. None = always. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Beat")
	FName RequiredGateFlag;

	/** World flag that completes this beat when it is set (for example Island1.SpanGateOpen, or Breakable.<barricade>). None = a trigger completes it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Beat")
	FName CompleteWorldFlag;

	/** Show the in-world marker (hidden inside WorldMarkerHideDistance). The compass marker is always shown while the gate is open. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Beat")
	bool bShowWorldMarker = true;
};

/** What the compass and tracker widgets need about the active beat. */
USTRUCT(BlueprintType)
struct BLACKWOODHOLLOWBETA_API FBH_ObjectiveInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Beat")
	FGameplayTag BeatId;

	UPROPERTY(BlueprintReadOnly, Category = "Beat")
	FText ObjectiveText;

	/** True when a target actor was found (it can be unresolved for a moment on a client). */
	UPROPERTY(BlueprintReadOnly, Category = "Beat")
	bool bHasTarget = false;

	UPROPERTY(BlueprintReadOnly, Category = "Beat")
	FVector TargetLocation = FVector::ZeroVector;

	/** The gate for this beat is open: the markers may show. When false, show the text only, no direction. */
	UPROPERTY(BlueprintReadOnly, Category = "Beat")
	bool bGateOpen = true;

	UPROPERTY(BlueprintReadOnly, Category = "Beat")
	bool bShowWorldMarker = true;
};

/** A party member as every machine sees them: replicated at about 4 Hz by the server, so it works for far players that are not net-relevant. */
USTRUCT(BlueprintType)
struct BLACKWOODHOLLOWBETA_API FBH_PartnerPing
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Beat")
	TObjectPtr<APlayerState> Player = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Beat")
	FVector_NetQuantize10 Location = FVector_NetQuantize10(0.0, 0.0, 0.0);

	/** EBH_DeathPhase as a byte: 0 = alive; anything else is down (1 dead, 2 awaiting choice, 3 waiting for revive). */
	UPROPERTY(BlueprintReadOnly, Category = "Beat")
	uint8 DeathPhase = 0;
};