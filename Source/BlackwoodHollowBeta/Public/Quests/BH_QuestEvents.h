
// Blackwood Hollow - the quest event vocabulary (Phase 14A-1)
// Target: Unreal Engine 5.8 (C++)
//
// Every gameplay event a quest can wait for is one Narrative DATA TASK: a task NAME (the constants below) plus an ARGUMENT string.
// UBH_QuestDirector::ReportQuestEvent(World, TaskName, Argument, Quantity) is the one entry point; it runs on the server and forwards to the party
// component's CompleteNarrativeDataTask. A quest asset waits for an event with the stock BPT_CompleteDataTask task (DataTask = the data task asset
// whose TaskName is one of these, Argument = the string; an EMPTY Argument matches any argument of that task).
//
//   TaskName        Argument                                         Reported by
//   --------------  -----------------------------------------------  --------------------------------------------------------------
//   OpenContainer   ChecklistItemId, else first actor tag, else the  ABH_LootContainer (opened), ABH_WeaponCache (taken)
//                   actor name
//   TakeItem        item class short name (class name without _C)    loot container rolls, weapon cache, enemy drops
//   WorldFlag       the flag name ("Island1.SpanGateOpen")           ABH_GameState::SetWorldFlag (flag newly set): barricade, Span Gate...
//   Kill            enemy identity = ScalingRowPrefix, else class    ABH_EnemyBase / UBH_CombatIdentityComponent death (server). A Veteran
//                   short name; "<identity>.<Rank>" for Veteran,     or higher also reports "<identity>.<Rank>" ("CorruptedCrab.Veteran").
//                   Elite, Boss
//   TalkTo          NPC id (the Narrative speaker / actor tag)       not hooked in 14A-1: call ReportQuestEvent from the dialogue / NPC Blueprint
//   EnterArea       ABH_BeatTrigger::QuestAreaId                     ABH_BeatTrigger (once per player per volume)
//   LearnSkill      "OverloadBurst"                                  ABH_PlayerState::GrantOverloadBurst (the Warden obelisk)
//   Relight         ABH_BeaconRelight world flag name                ABH_BeaconRelight (the Warden Watch beacon)
//   Attune          hub name ("PortVanguard")                        ABH_PlayerState::AttuneHub (a respawn point attuned for the first time)
//   Interact        first actor tag, else the actor name             UBH_InteractableComponent::NotifyInteractionCompleted (any interactable)
//
// The helpers below are pure (no world, no UObject) so the automation tests can pin the exact strings.

#pragma once

#include "CoreMinimal.h"

namespace BH_QuestEvents
{
	// -- Task names (the TaskName of the matching UNarrativeDataTask asset must be EXACTLY this text) ---------------------------
	inline constexpr const TCHAR* OpenContainer = TEXT("OpenContainer");
	inline constexpr const TCHAR* TakeItem = TEXT("TakeItem");
	inline constexpr const TCHAR* WorldFlag = TEXT("WorldFlag");
	inline constexpr const TCHAR* Kill = TEXT("Kill");
	inline constexpr const TCHAR* TalkTo = TEXT("TalkTo");
	inline constexpr const TCHAR* EnterArea = TEXT("EnterArea");
	inline constexpr const TCHAR* LearnSkill = TEXT("LearnSkill");
	inline constexpr const TCHAR* Relight = TEXT("Relight");
	inline constexpr const TCHAR* Attune = TEXT("Attune");
	inline constexpr const TCHAR* Interact = TEXT("Interact");

	// -- Fixed arguments ---------------------------------------------------------------------------------------------------------
	inline constexpr const TCHAR* SkillOverloadBurst = TEXT("OverloadBurst");

	/** Arguments are compared as text: surrounding whitespace never matters. */
	inline FString NormalizeArgument(const FString& Argument)
	{
		return Argument.TrimStartAndEnd();
	}

	/** "BPI_Weapon_Longsword_C" -> "BPI_Weapon_Longsword" (a Blueprint generated class name without its suffix). */
	inline FString ClassShortName(const FString& ClassName)
	{
		FString Result = ClassName.TrimStartAndEnd();
		if (Result.EndsWith(TEXT("_C"), ESearchCase::CaseSensitive))
		{
			Result.LeftChopInline(2);
		}
		return Result;
	}

	/** The Kill argument for a ranked kill: "<identity>.<rank>" ("CorruptedCrab.Veteran"). */
	inline FString MakeRankedKillArgument(const FString& Identity, const FString& RankName)
	{
		return Identity + TEXT(".") + RankName;
	}

	/**
	 * The raw string Narrative builds from a data task: "<TaskName>_<Argument>", lower case, spaces removed. The same rule as
	 * UNarrativeDataTask::MakeTaskString and UNarrativeComponent::CompleteNarrativeDataTask (the key of MasterTaskList); the director uses it as the
	 * key of its replicated "already reported" set.
	 */
	inline FString MakeTaskString(const FString& TaskName, const FString& Argument)
	{
		FString Result = (TaskName + TEXT("_") + Argument).ToLower();
		Result.RemoveSpacesInline();
		return Result;
	}

	/** Log / debug text: "OpenContainer(Wreck.Crate1)". */
	inline FString Describe(const FString& TaskName, const FString& Argument)
	{
		return FString::Printf(TEXT("%s(%s)"), *TaskName, *Argument);
	}
}
