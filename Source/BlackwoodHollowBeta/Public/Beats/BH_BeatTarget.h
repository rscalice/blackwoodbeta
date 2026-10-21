// Blackwood Hollow - beat target marker actor (Phase 13B+)
// Target: Unreal Engine 5.8 (C++)
//
// Place one in the level for each beat: it marks where the compass and the world marker point. It is NOT spatially loaded
// (World Partition), so it exists on every machine from the start, even when the player is far away. It does not replicate and has
// no collision, tick or visuals; move it to change where a beat points. UBH_BeatTrackerComponent finds it by BeatTag.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "BH_BeatTarget.generated.h"

UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API ABH_BeatTarget : public AActor
{
	GENERATED_BODY()

public:
	ABH_BeatTarget();

	/** The beat this actor is the target of (matches FBH_BeatRow::TargetTag, or BeatId when that is empty). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Beat", meta = (Categories = "Beat"))
	FGameplayTag BeatTag;

	/**
	 * Scavenging checklist. When set, this target is one item of the beat's checklist: several targets can share a BeatTag, each with its own
	 * id. The marker shows for a player until THEY have done the item (the interactable reports it through
	 * UBH_BeatTrackerComponent::ReportChecklistItem with the same id), the objective text gets a "(done/total)" counter, and the beat completes
	 * when any one player has done every item. None = an ordinary single-target beat.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Beat")
	FName ChecklistItemId;
};