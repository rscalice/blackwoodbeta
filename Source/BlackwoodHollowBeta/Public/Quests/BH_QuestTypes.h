
// Blackwood Hollow - quest data types (Phase 14A-1)
// Target: Unreal Engine 5.8 (C++)

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "Quest.h"
#include "BH_QuestTypes.generated.h"

/**
 * One row of DT_BH_QuestTargets: where the compass points while a quest task is active. A row is found by the (TaskName, Argument) of the data task
 * the quest task waits for (the stock BPT_CompleteDataTask exposes both: variables DataTask and Argument). A row with an EMPTY Argument is the
 * fallback for every argument of that TaskName. The row name is free ("OpenContainer.WreckCrate1").
 */
USTRUCT(BlueprintType)
struct BLACKWOODHOLLOWBETA_API FBH_QuestTargetRow : public FTableRowBase
{
	GENERATED_BODY()

	/** TaskName of the data task (BH_QuestEvents: "OpenContainer", "Kill", ...). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	FName TaskName;

	/** Argument of the task ("Wreck.Crate1"). Empty = every argument of TaskName. Compared case-insensitively, trimmed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	FString Argument;

	/** Quest.Target.* tag of the placed ABH_QuestTarget actor(s) to point at. Several actors may share a tag (a scavenging list). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest", meta = (Categories = "Quest.Target"))
	FGameplayTag TargetTag;

	/** World flag (ABH_GameState) that must be set before the marker shows, so it never points through a locked gate. None = always. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	FName RequiredGateFlag;

	/** Show the in-world marker (hidden inside UBH_BeatTrackerComponent::WorldMarkerHideDistance). The compass marker is always shown while the gate is open. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest")
	bool bShowWorldMarker = true;
};

/** One visible task of the current quest state, as the tracker widget and the compass need it. */
USTRUCT(BlueprintType)
struct BLACKWOODHOLLOWBETA_API FBH_QuestObjectiveLine
{
	GENERATED_BODY()

	/** The task description with its progress: "Open the supply crates (2/3)". The count only appears when the task needs more than one. */
	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	FText Text;

	/** The task description alone. */
	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	FText Description;

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	int32 Current = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	int32 Required = 1;

	/** Current >= Required (an optional task does not count as complete here). */
	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	bool bComplete = false;

	/** Data task name / argument the task waits for, when it exposes them (empty otherwise). */
	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	FString TaskName;

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	FString Argument;

	/** From the matching DT_BH_QuestTargets row (empty tag = no row, so nothing to point at). */
	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	FGameplayTag TargetTag;

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	FName RequiredGateFlag;

	/** True when RequiredGateFlag is none or set (evaluated when the objective was built). */
	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	bool bGateOpen = true;

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	bool bShowWorldMarker = true;
};

/** What the tracker widget and the compass need about the party's current quest objective. */
USTRUCT(BlueprintType)
struct BLACKWOODHOLLOWBETA_API FBH_QuestObjective
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	TSubclassOf<UQuest> QuestClass;

	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	FText QuestTitle;

	/** The quest is the one begun as the MAIN quest (UBH_QuestDirector::BeginPartyQuest bMainQuest). */
	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	bool bIsMainQuest = false;

	/** Every visible task of the current state (not capped; the compass uses all of them). */
	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	TArray<FBH_QuestObjectiveLine> Lines;

	/** The first UBH_QuestDirector::MaxObjectiveLines lines of Lines, one per text line ("A (1/3)\nB"). What the quest tracker shows. */
	UPROPERTY(BlueprintReadOnly, Category = "Quest")
	FText ObjectiveText;

	/** The first line that is not complete (the one to highlight), or INDEX_NONE. */
	int32 FindFirstOpenLine() const
	{
		for (int32 Index = 0; Index < Lines.Num(); ++Index)
		{
			if (!Lines[Index].bComplete)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	/** The line whose TargetTag is Tag. An open line wins over a complete one; null when no line points at Tag. */
	const FBH_QuestObjectiveLine* FindLineForTarget(const FGameplayTag& Tag) const
	{
		const FBH_QuestObjectiveLine* Found = nullptr;
		if (!Tag.IsValid())
		{
			return nullptr;
		}
		for (const FBH_QuestObjectiveLine& Line : Lines)
		{
			if (Line.TargetTag == Tag)
			{
				if (!Line.bComplete)
				{
					return &Line;
				}
				Found = Found ? Found : &Line;
			}
		}
		return Found;
	}
};
