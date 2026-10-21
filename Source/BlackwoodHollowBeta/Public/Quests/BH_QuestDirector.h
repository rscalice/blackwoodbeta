
// Blackwood Hollow - party quest director (Phase 14A-1)
// Target: Unreal Engine 5.8 (C++), Narrative 3.7.4
//
// Lives on ABH_GameState (one party per session), like the beat tracker. Phase 14 turns the 13B+ beat tracker into real Narrative quests; this
// component is the bridge between the game (events in, objective out) and the party's Narrative quests.
//
// ---------------------------------------------------------------------------------------------------------------------------------------
// PARTY QUEST FLOW (server)
//   1. Something calls BeginPartyQuest(QuestClass, bMainQuest). The quest is begun on UBH_PartyComponent (a UNarrativePartyComponent on the game
//      state), so it is the PARTY's quest: one shared instance, one shared progress, any player's event can advance it.
//      If the party has no members yet (the host's player state registers a moment after the game state begins play), the request is queued and
//      begun as soon as the first member is registered (ABH_GameState::OnPartyMembersChanged). That matters: UQuest::Initialize caches
//      UNarrativePartyComponent::GetOwningController(), which is found through the party's member list, so a quest begun on an empty party would
//      keep a null owning controller for every task.
//   2. Gameplay hooks call ReportQuestEvent(World, TaskName, Argument, Quantity) (server only; a no-op on clients, safe from anywhere). The
//      director forwards to UNarrativeComponent::CompleteNarrativeDataTask(TaskName, Argument, Quantity) on the party component, which
//      (a) looks the UNarrativeDataTask ASSET up by TaskName (UNarrativeTaskManager scans the asset registry; the asset's TaskName text must equal
//      the vocabulary constant EXACTLY, and a missing asset only logs a LogTemp error and broadcasts nothing), (b) broadcasts
//      OnNarrativeDataTaskCompleted(asset, Argument) on the party component, which quest tasks (the stock BPT_CompleteDataTask) listen to, and
//      (c) bumps the party's MasterTaskList. A task adds progress, a branch whose tasks are all done is taken, the quest enters its next state.
//   3. Events are forwarded only while a party quest is in progress (bRecordEventsWithoutQuest = false): until the 14A-2 assets exist there is no
//      data task asset to find, and every kill and flag would log an error. The director still remembers each distinct event in a replicated list
//      (ReportedEvents, used by ABH_QuestTarget::CompletedBy*) either way.
//
// WHAT REPLICATES TO CLIENTS (evidence: Narrative 3.7.4 NarrativeComponent.h / .cpp, NarrativePartyComponent.h / .cpp)
//   * UQuest objects do NOT replicate (NarrativeComponent.h: "UQuests aren't replicated the proper UE way"). Instead the server appends an
//     FNarrativeUpdate to UNarrativeComponent::PendingUpdateList (UPROPERTY Replicated, DOREPLIFETIME in GetLifetimeReplicatedProps) for
//     BeginQuest (UT_BeginQuest), restart / forget, and every task progress change (UT_TaskProgressMade: task index + new progress + branch id).
//     SendNarrativeUpdate skips it only in NM_Standalone. UT_QuestNewState is deliberately NOT sent for normal progress: the client reaches the new
//     state by itself when the replayed progress completes the branch (UNarrativeComponent::QuestNewState has that line commented out).
//   * A client's OnRep_PendingUpdateList replays every update whose bAcked is false, in order: BeginQuest(QuestClass) builds the same UQuest
//     instance locally, SetProgressInternal(..., bFromReplication) re-runs the task, the branch is taken, EnterState_Internal fires the same
//     delegates (OnQuestStarted, OnQuestNewState, OnQuestTaskProgressChanged, OnQuestTaskCompleted, OnQuestSucceeded, ...) on the CLIENT's copy of
//     the party component. This director binds those delegates on every machine, so OnObjectiveChanged fires everywhere.
//   * The party component lives on ABH_GameState (AGameStateBase is always relevant), so its PendingUpdateList replicates to every client, not
//     just the owner of a player controller. The member list that clients can read is UNarrativePartyComponent::PartyMemberStates (Replicated);
//     PartyMembers (the UNarrativeComponent list) is server only.
//   * Only data-task COMPLETION is server only: clients cannot report events, and the client copies of a task never see
//     OnNarrativeDataTaskCompleted (the server sends the progress, not the event).
//
// LATE JOINERS
//   Quests are the PARTY component's, not a member's. SendNarrativeUpdate never trims PendingUpdateList ("STALE UPDATE REMOVAL: Disabled" in the
//   Narrative source), so a client that connects later receives the whole history in its first replication bunch and replays it in order, ending
//   in exactly the server's quest states. Adding a member (UNarrativePartyComponent::AddPartyMember) therefore needs nothing: it only sets
//   Member->PartyComponent (so the member's own data tasks are forwarded to the party) and appends the player state to PartyMemberStates.
//   The director's own state (MainQuestClass, ReportedEvents) is a normal replicated property of this component. Caveat: the history grows for
//   the whole session (one small entry per progress change); the game has no save / load of quests yet.
//
// OBJECTIVE FOR THE UI (any machine)
//   GetCurrentObjective(FBH_QuestObjective&) reads the party's quest list: the MAIN quest (BeginPartyQuest bMainQuest, replicated) if it is in
//   progress and tracked, else the first tracked quest in progress. Its current state's visible branches / tasks become lines: the task description
//   (UNarrativeTask::GetTaskDescription) plus " (n/m)" when the task needs more than one (n from the task's CurrentProgress, read by reflection
//   because it is protected). Targets come from DT_BH_QuestTargets: the task's DataTask + Argument (BPT_CompleteDataTask variables, also read by
//   reflection) pick a row, the row names a Quest.Target.* tag, ABH_QuestTarget actors carrying that tag are the markers.
//   OnObjectiveChanged fires on every machine, one frame after the quest state changed (task progress is reset AFTER OnQuestNewState fires, so
//   reading in the delegate would see stale counts), and only when the objective text, targets or gate state actually changed.
//
// HOW QUESTS WILL BE AUTHORED (14A-2)
//   One UNarrativeDataTask asset per vocabulary entry (BH_QuestEvents.h), TaskName = the constant ("OpenContainer", "Kill", ...). A Narrative
//   quest asset (QuestBlueprint) has states; each state has branches; a branch holds BPT_CompleteDataTask tasks: DataTask = the asset, Argument =
//   the string, RequiredQuantity for counts, Description Override for the tracker line. Quest states start / chain other quests through Narrative
//   events or by calling BeginPartyQuest from a level / game Blueprint. Add one DT_BH_QuestTargets row per task that should point somewhere.
//
// 14A-2 (assets) MUST: create the data task assets and the first quests; place ABH_QuestTarget actors and add the Quest.Target.* children to
//   Config/Tags; fill DT_BH_QuestTargets (run Content/Python/bh_make_14a_quest_targets.py first); set bRecordEventsWithoutQuest = true once the
//   assets exist if tasks should count events that happened before their quest began (BPT_CompleteDataTask "Retroactive" reads the master list);
//   call BeginPartyQuest(MainQuest, true) at session start.
// 14A-3 (retire the tracker) MUST: run the golden path end to end on quests (listen server + a client + a late joiner), then remove
//   UBH_BeatTrackerComponent's objective use (the world-flag beat completion, ABH_BeatTrigger, ABH_BeatTarget, DT_BH_Beats), keep the partner pings
//   (they live on the tracker today and must move or stay), and switch UBH_BeatNavigationComponent / UBH_QuestTrackerWidget to the director only.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Quests/BH_QuestTypes.h"
#include "BH_QuestDirector.generated.h"

class AActor;
class ABH_QuestTarget;
class UBH_PartyComponent;
class UDataTable;

/** Any machine: the party's current quest objective (quest, state, task progress, targets or gate state) changed. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FBH_OnQuestObjectiveChanged);

/** One link of the quest chain the director runs by itself (Phase 14A-2b). Quests are Blueprint assets, so the class is a soft reference. */
USTRUCT(BlueprintType)
struct BLACKWOODHOLLOWBETA_API FBH_QuestChainEntry
{
	GENERATED_BODY()

	/** The quest asset's generated class (e.g. /Game/BlackwoodHollow/Quests/Q_KingsRoad.Q_KingsRoad_C). A missing asset logs a warning and the chain skips it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest")
	TSoftClassPtr<UQuest> QuestClass;

	/** XP given to every party member when the quest succeeds (through UBH_ProgressionComponent::AddXP, like kill XP). 0 = none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest", meta = (ClampMin = "0"))
	int32 XPReward = 0;

	/** Begin it as the party's main quest (the one the tracker shows first). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Quest")
	bool bMain = true;
};

UCLASS(ClassGroup = "BlackwoodHollow", meta = (BlueprintSpawnableComponent))
class BLACKWOODHOLLOWBETA_API UBH_QuestDirector : public UActorComponent
{
	GENERATED_BODY()

public:
	UBH_QuestDirector();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// -- Configuration -------------------------------------------------------------------------------------------

	/** The task -> target table (FBH_QuestTargetRow). Created by Content/Python/bh_make_14a_quest_targets.py; missing = quests just have no markers. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Quest")
	TSoftObjectPtr<UDataTable> QuestTargetTable;

	/** The tracker shows at most this many objective lines (FBH_QuestObjective::ObjectiveText). The compass still points at every line. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Quest", meta = (ClampMin = "1", ClampMax = "8"))
	int32 MaxObjectiveLines = 3;

	/** False: events are forwarded to Narrative only while a party quest is in progress. True: always (the master task list then also counts events from before a quest began). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Quest")
	bool bRecordEventsWithoutQuest = false;

	/** Upper bound of the replicated list of distinct reported events. Beyond it new events are still forwarded to Narrative, just not remembered. */
	UPROPERTY(EditDefaultsOnly, Category = "Quest", meta = (ClampMin = "16"))
	int32 MaxRecordedEvents = 512;

	/** The quests the director runs in order, with no Blueprint wiring: the first not-yet-started one begins on an auto-start level, each success begins the next and grants its XP. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Quest|Chain")
	TArray<FBH_QuestChainEntry> QuestChain;

	/** Master switch for the chain (auto-start, chaining and quest XP). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Quest|Chain")
	bool bAutoStartQuestChain = true;

	/** Levels (map name without the PIE prefix) on which the chain starts by itself when play begins on the server. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Quest|Chain")
	TArray<FName> AutoStartLevels;

	// -- Statics -------------------------------------------------------------------------------------------------

	/** Any machine. The director of Context's world, or null (no game state yet). */
	UFUNCTION(BlueprintPure, Category = "Quest", meta = (WorldContext = "WorldContext"))
	static UBH_QuestDirector* Get(const UObject* WorldContext);

	/**
	 * SERVER, safe from anywhere: reports one quest event (see BH_QuestEvents.h for the vocabulary). A no-op on a client, with no game state, or
	 * with an empty name / argument. @param TaskName a BH_QuestEvents constant. @param Argument the event argument. @param Quantity how many times.
	 */
	UFUNCTION(BlueprintCallable, Category = "Quest", meta = (WorldContext = "WorldContext"))
	static void ReportQuestEvent(const UObject* WorldContext, FName TaskName, const FString& Argument, int32 Quantity = 1);

	/** SERVER. Reports the kill of Enemy: Kill(<identity>) and, for Veteran or higher, Kill(<identity>.<Rank>). Call once per death, next to the XP grant. */
	static void ReportEnemyKill(const AActor* Enemy);

	/** SERVER. Reports TakeItem(<item class short name>) for ItemClass x Quantity. A null class or a non-positive quantity does nothing. */
	static void ReportItemTaken(const UObject* WorldContext, const UClass* ItemClass, int32 Quantity);

	// -- Pure helpers (no world needed; pinned by BlackwoodHollow.Phase14.Quest.*) -----------------------------------

	/** The Kill argument for an enemy: its ScalingRowPrefix, else the class short name. Empty for null. Works for ABH_EnemyBase and identity-component characters. */
	static FString GetEnemyIdentity(const AActor* Enemy);

	/** An argument naming Actor: PreferredId if set, else the actor's first tag, else its name. Empty for null. */
	static FString MakeActorArgument(const AActor* Actor, FName PreferredId);

	/** "Description (n/m)" when Required > 1 (n clamped to 0..Required), else Description alone. */
	static FText FormatObjectiveLine(const FText& Description, int32 Current, int32 Required);

	/** The first MaxLines lines of Lines, joined with one line break between them. Empty for no lines. */
	static FText BuildObjectiveText(const TArray<FBH_QuestObjectiveLine>& Lines, int32 MaxLines);

	/** True when two objectives look the same to the UI: quest, title, text, counts, targets and gate state of every line. */
	static bool AreObjectivesEqual(const FBH_QuestObjective& A, const FBH_QuestObjective& B);

	/** The default chain: Q_AmongTheWrack (60 XP), Q_KingsRoad (150), Q_GuardiansOfTheSpan (100), all main quests. */
	static TArray<FBH_QuestChainEntry> MakeDefaultQuestChain();

	/** The first index greater than AfterIndex (-1 = from the start) whose Handled flag is false (started / finished / unloadable). INDEX_NONE when there is none. */
	static int32 SelectNextChainIndex(const TArray<bool>& Handled, int32 AfterIndex);

	/** The index of the entry whose class path equals QuestClassPath, or INDEX_NONE. */
	static int32 FindChainIndex(const TArray<FBH_QuestChainEntry>& Chain, const FSoftObjectPath& QuestClassPath);

	/** True when MapName (the PIE prefix "UEDPIE_n_" and any package path are ignored) is one of Levels. */
	static bool IsAutoStartLevel(const TArray<FName>& Levels, const FString& MapName);

	// -- Server ----------------------------------------------------------------------------------------------------

	/**
	 * SERVER. Begins QuestClass as a PARTY quest (see the design note). bMainQuest makes it the quest the UI shows first (replicated).
	 * An empty party queues the request until the first member is registered. @return true if the quest was begun or queued; false for a null /
	 * base class, a client, or a quest the party already has (in progress or finished: use Narrative's RestartQuest for a replay).
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Quest")
	bool BeginPartyQuest(TSubclassOf<UQuest> QuestClass, bool bMainQuest = false, FName StartFromID = NAME_None);

	/**
	 * SERVER, testing (bh.Quest.Skip). Succeeds the WHOLE current quest: the in-progress chain quest, else the tracked quest. Narrative has no clean
	 * "complete this branch" call (branches complete from their tasks), so the whole quest is succeeded; the normal success path runs (XP, next chain quest).
	 * @return true if a quest was succeeded.
	 */
	bool DebugSkipCurrentQuest();

	/** SERVER. See ReportQuestEvent (the instance form). */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Quest")
	void ReportEvent(FName TaskName, const FString& Argument, int32 Quantity = 1);

	// -- Any machine -----------------------------------------------------------------------------------------------

	/** Any machine. Fills Out from the party's main (else first tracked) quest in progress. False when there is none. */
	UFUNCTION(BlueprintCallable, Category = "Quest")
	bool GetCurrentObjective(FBH_QuestObjective& Out);

	/** Any machine. The quest class begun as the main quest (null until BeginPartyQuest(..., true)). */
	UFUNCTION(BlueprintPure, Category = "Quest")
	TSubclassOf<UQuest> GetMainQuestClass() const { return MainQuestClass; }

	/** Any machine. True when the event (TaskName, Argument) has been reported at least once this session (party-wide; replicated). */
	UFUNCTION(BlueprintPure, Category = "Quest")
	bool HasEventBeenReported(FName TaskName, const FString& Argument) const;

	/** Any machine. True when Flag is none or set on the game state. */
	UFUNCTION(BlueprintPure, Category = "Quest")
	bool IsGateOpen(FName Flag) const;

	/**
	 * Any machine. Is Target an active marker of Objective? True when one of its lines points at the target's tag, that line is open and the
	 * target's CompletedBy event has not been reported. Fills the compass label (the line text), whether the gate is open (row gate AND the target's own
	 * gate) and whether the in-world marker may show.
	 */
	bool ResolveTarget(const ABH_QuestTarget* Target, const FBH_QuestObjective& Objective, FText& OutLabel, bool& bOutGateOpen, bool& bOutShowWorldMarker) const;

	/** Any machine, when the objective (text, targets, gate state) may have changed. */
	UPROPERTY(BlueprintAssignable, Category = "Quest")
	FBH_OnQuestObjectiveChanged OnObjectiveChanged;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** The quest class the UI shows first. Set by BeginPartyQuest(..., true). */
	UPROPERTY(ReplicatedUsing = OnRep_MainQuestClass)
	TSubclassOf<UQuest> MainQuestClass;

	/** Distinct "<taskname>_<argument>" strings (BH_QuestEvents::MakeTaskString) reported so far. */
	UPROPERTY(ReplicatedUsing = OnRep_ReportedEvents)
	TArray<FString> ReportedEvents;

	UFUNCTION()
	void OnRep_MainQuestClass();

	UFUNCTION()
	void OnRep_ReportedEvents();

	// Narrative party delegates (every machine; the client copies fire while OnRep_PendingUpdateList replays).
	UFUNCTION()
	void HandleQuestListChanged(const UQuest* Quest);

	UFUNCTION()
	void HandleQuestNewState(UQuest* Quest, const UQuestState* NewState);

	UFUNCTION()
	void HandleTaskProgress(const UQuest* Quest, const UNarrativeTask* ProgressedTask, const UQuestBranch* Branch, int32 OldProgress, int32 NewProgress);

	UFUNCTION()
	void HandleTaskCompleted(const UQuest* Quest, const UNarrativeTask* CompletedTask, const UQuestBranch* Branch);

	UFUNCTION()
	void HandleQuestEnded(const UQuest* Quest, const FText& Message);

	UFUNCTION()
	void HandleQuestSucceeded(const UQuest* Quest, const FText& Message);

	UFUNCTION()
	void HandleWorldFlagChanged(FName Flag, bool bIsSet);

	UFUNCTION()
	void HandlePartyMembersChanged();

private:
	UBH_PartyComponent* GetParty() const;
	void BindParty();
	void UnbindParty();
	void FlushPendingQuests();
	void LoadRows();
	const FBH_QuestTargetRow* FindRow(const FString& TaskName, const FString& Argument) const;
	UQuest* PickTrackedQuest(const UBH_PartyComponent& Party) const;
	void RecordEvent(const FString& TaskString);

	// Quest chain (server).
	void TryAutoStartChain();
	/** Begins the first unhandled chain entry after AfterIndex; entries that fail to load are skipped with a warning. */
	void BeginNextChainEntry(int32 AfterIndex);
	void GrantChainXP(int32 Amount);

	/** Queues one objective re-check for the next tick (a quest state change and its task resets happen inside one call). */
	void ScheduleObjectiveRefresh(bool bForceBroadcast);
	void FlushObjectiveRefresh();

	struct FPendingQuest
	{
		TSubclassOf<UQuest> QuestClass;
		FName StartFromID;
	};
	TArray<FPendingQuest> PendingQuests;

	TArray<FBH_QuestTargetRow> Rows;
	bool bRowsLoaded = false;

	FBH_QuestObjective LastObjective;
	bool bLastHadObjective = false;
	bool bRefreshQueued = false;
	bool bForceBroadcast = false;
	bool bBoundParty = false;
	bool bBoundGameState = false;

	/** Server: quest classes whose chain XP was already given (once per quest per party). */
	TSet<const UClass*> ChainXPGranted;
};
