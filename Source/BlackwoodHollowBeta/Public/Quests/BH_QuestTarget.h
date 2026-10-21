
// Blackwood Hollow - quest target marker actor (Phase 14A-1)
// Target: Unreal Engine 5.8 (C++)
//
// The quest twin of ABH_BeatTarget: place one in the level wherever a quest task should point the compass. It is NOT spatially loaded
// (World Partition), so it exists on every machine from the start, even when the player is far away. It does not replicate and has no collision,
// tick or visuals; move it to change where a task points. UBH_BeatNavigationComponent finds every placed target and shows the markers of the ones
// the party's current quest objective points at: a row of DT_BH_QuestTargets (FBH_QuestTargetRow) maps the (task, argument) a quest task waits
// for to a Quest.Target.* tag, and the targets carrying that tag are the markers.
//
// Several targets may share a tag (a scavenging list). To make a marker drop out once ITS part is done (a crate that was opened), set
// CompletedByTaskName / CompletedByArgument to the event that finishes it: once UBH_QuestDirector has seen that event, party-wide, the marker hides
// (the beat tracker's checklist was per player; a quest is shared by the whole party).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "BH_QuestTarget.generated.h"

UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API ABH_QuestTarget : public AActor
{
	GENERATED_BODY()

public:
	ABH_QuestTarget();

	/** The Quest.Target.* tag this actor answers to (matches FBH_QuestTargetRow::TargetTag). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest", meta = (Categories = "Quest.Target"))
	FGameplayTag QuestTag;

	/** Optional extra gate: a world flag (ABH_GameState) that must also be set before THIS marker shows. None = only the row's gate applies. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	FName RequiredGateFlag;

	/** Optional: the data task name (BH_QuestEvents) whose report finishes this target ("OpenContainer"). None = the marker stays while the task is open. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	FName CompletedByTaskName;

	/** ... and its argument ("Wreck.Crate1"). Both must be set; the pair is looked up in the director's replicated list of reported events. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	FString CompletedByArgument;
};
