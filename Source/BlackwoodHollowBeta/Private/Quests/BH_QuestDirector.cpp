
// Blackwood Hollow - party quest director (Phase 14A-1, implementation)

#include "Quests/BH_QuestDirector.h"
#include "Quests/BH_QuestEvents.h"
#include "Quests/BH_QuestTarget.h"
#include "Characters/BH_EnemyBase.h"
#include "Combat/BH_CombatIdentityComponent.h"
#include "Combat/BH_EnemyRank.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"
#include "NarrativeDataTask.h"
#include "Player/BH_GameState.h"
#include "Player/BH_PartyComponent.h"
#include "Player/BH_PartyLibrary.h"
#include "Progression/BH_ProgressionComponent.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Misc/PackageName.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogBHQuestDirector, Log, All);

namespace
{
	/** UNarrativeTask::CurrentProgress is protected (no getter); the reflection system does not care. */
	int32 ReadTaskProgress(const UNarrativeTask* Task)
	{
		static const FIntProperty* ProgressProperty = CastField<FIntProperty>(UNarrativeTask::StaticClass()->FindPropertyByName(TEXT("CurrentProgress")));
		if (ProgressProperty)
		{
			return ProgressProperty->GetPropertyValue_InContainer(Task);
		}
		return Task->IsComplete() ? Task->RequiredQuantity : 0;
	}

	/**
	 * The data task a quest task waits for. The stock BPT_CompleteDataTask has the Blueprint variables "DataTask" (UNarrativeDataTask) and
	 * "Argument" (string); a custom task class that names its variables the same way works too. False for any other task (no target lookup then).
	 */
	bool ReadTaskDataKey(const UNarrativeTask* Task, FString& OutTaskName, FString& OutArgument)
	{
		const UClass* TaskClass = Task->GetClass();
		const FObjectProperty* DataProperty = CastField<FObjectProperty>(TaskClass->FindPropertyByName(TEXT("DataTask")));
		const FStrProperty* ArgumentProperty = CastField<FStrProperty>(TaskClass->FindPropertyByName(TEXT("Argument")));
		if (!DataProperty || !ArgumentProperty)
		{
			return false;
		}
		const UNarrativeDataTask* DataTask = Cast<UNarrativeDataTask>(DataProperty->GetObjectPropertyValue_InContainer(Task));
		if (!DataTask)
		{
			return false;
		}
		OutTaskName = DataTask->TaskName;
		OutArgument = BH_QuestEvents::NormalizeArgument(ArgumentProperty->GetPropertyValue_InContainer(Task));
		return true;
	}
}

UBH_QuestDirector::UBH_QuestDirector()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	QuestTargetTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/BlackwoodHollow/Quests/DT_BH_QuestTargets.DT_BH_QuestTargets")));
	QuestChain = MakeDefaultQuestChain();
	AutoStartLevels.Add(FName(TEXT("L_WreckageShallows")));
}

void UBH_QuestDirector::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UBH_QuestDirector, MainQuestClass);
	DOREPLIFETIME(UBH_QuestDirector, ReportedEvents);
}

UBH_QuestDirector* UBH_QuestDirector::Get(const UObject* WorldContext)
{
	const ABH_GameState* GameState = ABH_GameState::Get(WorldContext);
	return GameState ? GameState->GetQuestDirector() : nullptr;
}

UBH_PartyComponent* UBH_QuestDirector::GetParty() const
{
	const ABH_GameState* GameState = Cast<ABH_GameState>(GetOwner());
	return GameState ? GameState->GetPartyComponent() : nullptr;
}

// ============================================================================
// Lifecycle
// ============================================================================

void UBH_QuestDirector::BeginPlay()
{
	Super::BeginPlay();

	if (ABH_GameState* GameState = Cast<ABH_GameState>(GetOwner()))
	{
		GameState->OnWorldFlagChanged.AddUniqueDynamic(this, &UBH_QuestDirector::HandleWorldFlagChanged);
		GameState->OnPartyMembersChanged.AddUniqueDynamic(this, &UBH_QuestDirector::HandlePartyMembersChanged);
		bBoundGameState = true;
	}
	BindParty();
	TryAutoStartChain();

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		FlushPendingQuests();
	}
	ScheduleObjectiveRefresh(false);
}

void UBH_QuestDirector::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bBoundGameState)
	{
		if (ABH_GameState* GameState = Cast<ABH_GameState>(GetOwner()))
		{
			GameState->OnWorldFlagChanged.RemoveDynamic(this, &UBH_QuestDirector::HandleWorldFlagChanged);
			GameState->OnPartyMembersChanged.RemoveDynamic(this, &UBH_QuestDirector::HandlePartyMembersChanged);
		}
		bBoundGameState = false;
	}
	UnbindParty();
	PendingQuests.Reset();
	Super::EndPlay(EndPlayReason);
}

void UBH_QuestDirector::BindParty()
{
	UBH_PartyComponent* Party = GetParty();
	if (!Party || bBoundParty)
	{
		return;
	}
	Party->OnQuestStarted.AddUniqueDynamic(this, &UBH_QuestDirector::HandleQuestListChanged);
	Party->OnQuestForgotten.AddUniqueDynamic(this, &UBH_QuestDirector::HandleQuestListChanged);
	Party->OnQuestRestarted.AddUniqueDynamic(this, &UBH_QuestDirector::HandleQuestListChanged);
	Party->OnQuestNewState.AddUniqueDynamic(this, &UBH_QuestDirector::HandleQuestNewState);
	Party->OnQuestTaskProgressChanged.AddUniqueDynamic(this, &UBH_QuestDirector::HandleTaskProgress);
	Party->OnQuestTaskCompleted.AddUniqueDynamic(this, &UBH_QuestDirector::HandleTaskCompleted);
	Party->OnQuestSucceeded.AddUniqueDynamic(this, &UBH_QuestDirector::HandleQuestSucceeded);
	Party->OnQuestFailed.AddUniqueDynamic(this, &UBH_QuestDirector::HandleQuestEnded);
	bBoundParty = true;
}

void UBH_QuestDirector::UnbindParty()
{
	if (!bBoundParty)
	{
		return;
	}
	if (UBH_PartyComponent* Party = GetParty())
	{
		Party->OnQuestStarted.RemoveDynamic(this, &UBH_QuestDirector::HandleQuestListChanged);
		Party->OnQuestForgotten.RemoveDynamic(this, &UBH_QuestDirector::HandleQuestListChanged);
		Party->OnQuestRestarted.RemoveDynamic(this, &UBH_QuestDirector::HandleQuestListChanged);
		Party->OnQuestNewState.RemoveDynamic(this, &UBH_QuestDirector::HandleQuestNewState);
		Party->OnQuestTaskProgressChanged.RemoveDynamic(this, &UBH_QuestDirector::HandleTaskProgress);
		Party->OnQuestTaskCompleted.RemoveDynamic(this, &UBH_QuestDirector::HandleTaskCompleted);
		Party->OnQuestSucceeded.RemoveDynamic(this, &UBH_QuestDirector::HandleQuestSucceeded);
		Party->OnQuestFailed.RemoveDynamic(this, &UBH_QuestDirector::HandleQuestEnded);
	}
	bBoundParty = false;
}

// ============================================================================
// Server: begin quests, report events
// ============================================================================

bool UBH_QuestDirector::BeginPartyQuest(TSubclassOf<UQuest> QuestClass, bool bMainQuest, FName StartFromID)
{
	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority() || !QuestClass || QuestClass == UQuest::StaticClass())
	{
		return false;
	}
	UBH_PartyComponent* Party = GetParty();
	if (!Party)
	{
		return false;
	}

	if (bMainQuest && MainQuestClass != QuestClass)
	{
		MainQuestClass = QuestClass;
		GetOwner()->ForceNetUpdate();
		OnRep_MainQuestClass(); // the server does not get the RepNotify
	}

	if (Party->GetQuestInstance(QuestClass))
	{
		UE_LOG(LogBHQuestDirector, Log, TEXT("BeginPartyQuest: the party already has %s."), *GetNameSafe(QuestClass.Get()));
		return false;
	}

	// A party without members has no owning controller yet (see the design note): begin the quest when the first member is registered.
	if (Party->GetPartyMemberStates().Num() == 0)
	{
		for (const FPendingQuest& Pending : PendingQuests)
		{
			if (Pending.QuestClass == QuestClass)
			{
				return true;
			}
		}
		FPendingQuest Queued;
		Queued.QuestClass = QuestClass;
		Queued.StartFromID = StartFromID;
		PendingQuests.Add(Queued);
		UE_LOG(LogBHQuestDirector, Log, TEXT("BeginPartyQuest: %s queued until the party has a member."), *GetNameSafe(QuestClass.Get()));
		return true;
	}

	UQuest* Begun = Party->BeginQuest(QuestClass, StartFromID);
	UE_LOG(LogBHQuestDirector, Log, TEXT("BeginPartyQuest: %s %s."), *GetNameSafe(QuestClass.Get()), Begun ? TEXT("begun") : TEXT("could not be begun"));
	return Begun != nullptr;
}

void UBH_QuestDirector::FlushPendingQuests()
{
	UBH_PartyComponent* Party = GetParty();
	if (!Party || PendingQuests.IsEmpty() || Party->GetPartyMemberStates().Num() == 0)
	{
		return;
	}
	const TArray<FPendingQuest> ToBegin = MoveTemp(PendingQuests);
	PendingQuests.Reset();
	for (const FPendingQuest& Pending : ToBegin)
	{
		if (Pending.QuestClass && !Party->GetQuestInstance(Pending.QuestClass))
		{
			UQuest* Begun = Party->BeginQuest(Pending.QuestClass, Pending.StartFromID);
			UE_LOG(LogBHQuestDirector, Log, TEXT("Queued quest %s %s."), *GetNameSafe(Pending.QuestClass.Get()), Begun ? TEXT("begun") : TEXT("could not be begun"));
		}
	}
}

void UBH_QuestDirector::HandlePartyMembersChanged()
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		FlushPendingQuests();
	}
}

void UBH_QuestDirector::ReportEvent(FName TaskName, const FString& Argument, int32 Quantity)
{
	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority() || TaskName.IsNone() || Quantity <= 0)
	{
		return;
	}
	const FString Task = TaskName.ToString();
	const FString TrimmedArgument = BH_QuestEvents::NormalizeArgument(Argument);
	if (TrimmedArgument.IsEmpty())
	{
		return; // Narrative refuses an empty argument
	}

	RecordEvent(BH_QuestEvents::MakeTaskString(Task, TrimmedArgument));

	UBH_PartyComponent* Party = GetParty();
	if (!Party)
	{
		return;
	}
	if (!bRecordEventsWithoutQuest && Party->GetInProgressQuests().IsEmpty())
	{
		UE_LOG(LogBHQuestDirector, VeryVerbose, TEXT("Event %s ignored: no party quest in progress."), *BH_QuestEvents::Describe(Task, TrimmedArgument));
		return;
	}
	UE_LOG(LogBHQuestDirector, Log, TEXT("Event %s x%d"), *BH_QuestEvents::Describe(Task, TrimmedArgument), Quantity);
	Party->CompleteNarrativeDataTask(Task, TrimmedArgument, Quantity);
}

void UBH_QuestDirector::ReportQuestEvent(const UObject* WorldContext, FName TaskName, const FString& Argument, int32 Quantity)
{
	if (UBH_QuestDirector* Director = Get(WorldContext))
	{
		Director->ReportEvent(TaskName, Argument, Quantity);
	}
}

void UBH_QuestDirector::ReportEnemyKill(const AActor* Enemy)
{
	if (!Enemy || !Enemy->HasAuthority())
	{
		return;
	}
	const FString Identity = GetEnemyIdentity(Enemy);
	if (Identity.IsEmpty())
	{
		return;
	}
	ReportQuestEvent(Enemy, FName(BH_QuestEvents::Kill), Identity, 1);

	const EBH_EnemyRank Rank = UBH_EnemyRankLibrary::GetRank(Enemy);
	if (Rank != EBH_EnemyRank::Common)
	{
		ReportQuestEvent(Enemy, FName(BH_QuestEvents::Kill), BH_QuestEvents::MakeRankedKillArgument(Identity, UBH_EnemyRankLibrary::RankName(Rank)), 1);
	}
}

void UBH_QuestDirector::ReportItemTaken(const UObject* WorldContext, const UClass* ItemClass, int32 Quantity)
{
	if (!ItemClass || Quantity <= 0)
	{
		return;
	}
	ReportQuestEvent(WorldContext, FName(BH_QuestEvents::TakeItem), BH_QuestEvents::ClassShortName(ItemClass->GetName()), Quantity);
}

void UBH_QuestDirector::RecordEvent(const FString& TaskString)
{
	if (ReportedEvents.Contains(TaskString) || ReportedEvents.Num() >= MaxRecordedEvents)
	{
		return;
	}
	ReportedEvents.Add(TaskString);
	if (AActor* OwnerActor = GetOwner())
	{
		OwnerActor->ForceNetUpdate();
	}
	OnRep_ReportedEvents(); // the server does not get the RepNotify
}

bool UBH_QuestDirector::HasEventBeenReported(FName TaskName, const FString& Argument) const
{
	if (TaskName.IsNone())
	{
		return false;
	}
	return ReportedEvents.Contains(BH_QuestEvents::MakeTaskString(TaskName.ToString(), BH_QuestEvents::NormalizeArgument(Argument)));
}

bool UBH_QuestDirector::IsGateOpen(FName Flag) const
{
	if (Flag.IsNone())
	{
		return true;
	}
	const ABH_GameState* GameState = Cast<ABH_GameState>(GetOwner());
	return GameState && GameState->HasWorldFlag(Flag);
}

// ============================================================================
// Pure helpers
// ============================================================================

FString UBH_QuestDirector::GetEnemyIdentity(const AActor* Enemy)
{
	if (!Enemy)
	{
		return FString();
	}
	FName Prefix;
	if (const ABH_EnemyBase* EnemyCharacter = Cast<ABH_EnemyBase>(Enemy))
	{
		Prefix = EnemyCharacter->ScalingRowPrefix;
	}
	else if (const UBH_CombatIdentityComponent* Identity = UBH_CombatIdentityComponent::Find(Enemy))
	{
		Prefix = Identity->ScalingRowPrefix;
	}
	if (!Prefix.IsNone())
	{
		return Prefix.ToString();
	}
	return BH_QuestEvents::ClassShortName(Enemy->GetClass()->GetName());
}

FString UBH_QuestDirector::MakeActorArgument(const AActor* Actor, FName PreferredId)
{
	if (!Actor)
	{
		return FString();
	}
	if (!PreferredId.IsNone())
	{
		return PreferredId.ToString();
	}
	for (const FName& ActorTag : Actor->Tags)
	{
		if (!ActorTag.IsNone())
		{
			return ActorTag.ToString();
		}
	}
	return Actor->GetFName().ToString();
}

FText UBH_QuestDirector::FormatObjectiveLine(const FText& Description, int32 Current, int32 Required)
{
	if (Required <= 1)
	{
		return Description;
	}
	const int32 Shown = FMath::Clamp(Current, 0, Required);
	return FText::Format(NSLOCTEXT("BlackwoodHollow", "QuestObjectiveProgress", "{0} ({1}/{2})"), Description, Shown, Required);
}

FText UBH_QuestDirector::BuildObjectiveText(const TArray<FBH_QuestObjectiveLine>& Lines, int32 MaxLines)
{
	TArray<FText> Texts;
	for (const FBH_QuestObjectiveLine& Line : Lines)
	{
		if (Texts.Num() >= MaxLines)
		{
			break;
		}
		Texts.Add(Line.Text);
	}
	return Texts.IsEmpty() ? FText::GetEmpty() : FText::Join(FText::FromString(TEXT("\n")), Texts);
}

bool UBH_QuestDirector::AreObjectivesEqual(const FBH_QuestObjective& A, const FBH_QuestObjective& B)
{
	if (A.QuestClass != B.QuestClass || A.bIsMainQuest != B.bIsMainQuest || !A.QuestTitle.EqualTo(B.QuestTitle) || !A.ObjectiveText.EqualTo(B.ObjectiveText) || A.Lines.Num() != B.Lines.Num())
	{
		return false;
	}
	for (int32 Index = 0; Index < A.Lines.Num(); ++Index)
	{
		const FBH_QuestObjectiveLine& LineA = A.Lines[Index];
		const FBH_QuestObjectiveLine& LineB = B.Lines[Index];
		if (!LineA.Text.EqualTo(LineB.Text) || LineA.Current != LineB.Current || LineA.Required != LineB.Required || LineA.bComplete != LineB.bComplete
			|| LineA.TargetTag != LineB.TargetTag || LineA.RequiredGateFlag != LineB.RequiredGateFlag || LineA.bGateOpen != LineB.bGateOpen
			|| LineA.bShowWorldMarker != LineB.bShowWorldMarker)
		{
			return false;
		}
	}
	return true;
}

// ============================================================================
// Objective (any machine)
// ============================================================================

void UBH_QuestDirector::LoadRows()
{
	if (bRowsLoaded)
	{
		return;
	}
	bRowsLoaded = true;
	Rows.Reset();
	if (QuestTargetTable.IsNull())
	{
		return;
	}
	const UDataTable* Table = QuestTargetTable.LoadSynchronous();
	if (!Table)
	{
		UE_LOG(LogBHQuestDirector, Log, TEXT("Quest target table %s did not load: quests will have no markers until it exists (run bh_make_14a_quest_targets.py)."), *QuestTargetTable.ToString());
		return;
	}
	for (const FName& RowName : Table->GetRowNames())
	{
		if (const FBH_QuestTargetRow* Row = Table->FindRow<FBH_QuestTargetRow>(RowName, TEXT("BH_QuestDirector")))
		{
			Rows.Add(*Row);
		}
	}
}

const FBH_QuestTargetRow* UBH_QuestDirector::FindRow(const FString& TaskName, const FString& Argument) const
{
	const FBH_QuestTargetRow* Fallback = nullptr;
	for (const FBH_QuestTargetRow& Row : Rows)
	{
		if (!Row.TaskName.ToString().Equals(TaskName, ESearchCase::IgnoreCase))
		{
			continue;
		}
		const FString RowArgument = BH_QuestEvents::NormalizeArgument(Row.Argument);
		if (RowArgument.Equals(Argument, ESearchCase::IgnoreCase))
		{
			return &Row;
		}
		if (RowArgument.IsEmpty() && !Fallback)
		{
			Fallback = &Row;
		}
	}
	return Fallback;
}

UQuest* UBH_QuestDirector::PickTrackedQuest(const UBH_PartyComponent& Party) const
{
	UQuest* FirstTracked = nullptr;
	for (UQuest* Quest : Party.GetInProgressQuests())
	{
		if (!Quest || !Quest->IsTracked() || !Quest->GetCurrentState())
		{
			continue;
		}
		if (MainQuestClass && Quest->GetClass()->IsChildOf(MainQuestClass.Get()))
		{
			return Quest;
		}
		if (!FirstTracked)
		{
			FirstTracked = Quest;
		}
	}
	return FirstTracked;
}

bool UBH_QuestDirector::GetCurrentObjective(FBH_QuestObjective& Out)
{
	Out = FBH_QuestObjective();
	const UBH_PartyComponent* Party = GetParty();
	UQuest* Quest = Party ? PickTrackedQuest(*Party) : nullptr;
	const UQuestState* State = Quest ? Quest->GetCurrentState() : nullptr;
	if (!State)
	{
		return false;
	}
	LoadRows();

	Out.QuestClass = Quest->GetClass();
	Out.QuestTitle = Quest->GetQuestName();
	Out.bIsMainQuest = MainQuestClass && Quest->GetClass()->IsChildOf(MainQuestClass.Get());

	for (const UQuestBranch* Branch : State->Branches)
	{
		if (!Branch || Branch->bHidden)
		{
			continue;
		}
		for (const UNarrativeTask* Task : Branch->QuestTasks)
		{
			if (!Task || Task->bHidden)
			{
				continue;
			}
			FBH_QuestObjectiveLine Line;
			Line.Description = Task->GetTaskDescription();
			Line.Required = FMath::Max(1, Task->RequiredQuantity);
			Line.Current = FMath::Clamp(ReadTaskProgress(Task), 0, Line.Required);
			Line.bComplete = Line.Current >= Line.Required;
			Line.Text = FormatObjectiveLine(Line.Description, Line.Current, Line.Required);

			if (ReadTaskDataKey(Task, Line.TaskName, Line.Argument))
			{
				if (const FBH_QuestTargetRow* Row = FindRow(Line.TaskName, Line.Argument))
				{
					Line.TargetTag = Row->TargetTag;
					Line.RequiredGateFlag = Row->RequiredGateFlag;
					Line.bShowWorldMarker = Row->bShowWorldMarker;
					Line.bGateOpen = IsGateOpen(Row->RequiredGateFlag);
				}
			}
			Out.Lines.Add(Line);
		}
	}

	if (Out.Lines.IsEmpty() && !State->Description.IsEmptyOrWhitespace())
	{
		// A state with no visible task (a story beat): its journal text is the objective.
		FBH_QuestObjectiveLine Line;
		Line.Description = State->Description;
		Line.Text = State->Description;
		Out.Lines.Add(Line);
	}

	Out.ObjectiveText = BuildObjectiveText(Out.Lines, MaxObjectiveLines);
	return !Out.Lines.IsEmpty();
}

bool UBH_QuestDirector::ResolveTarget(const ABH_QuestTarget* Target, const FBH_QuestObjective& Objective, FText& OutLabel, bool& bOutGateOpen, bool& bOutShowWorldMarker) const
{
	if (!Target || !Target->QuestTag.IsValid())
	{
		return false;
	}
	const FBH_QuestObjectiveLine* Line = Objective.FindLineForTarget(Target->QuestTag);
	if (!Line || Line->bComplete)
	{
		return false;
	}
	if (!Target->CompletedByTaskName.IsNone() && HasEventBeenReported(Target->CompletedByTaskName, Target->CompletedByArgument))
	{
		return false;
	}
	OutLabel = Line->Text;
	bOutShowWorldMarker = Line->bShowWorldMarker;
	bOutGateOpen = IsGateOpen(Line->RequiredGateFlag) && IsGateOpen(Target->RequiredGateFlag);
	return true;
}

// ============================================================================
// Change notification
// ============================================================================

void UBH_QuestDirector::ScheduleObjectiveRefresh(bool bForce)
{
	bForceBroadcast |= bForce;
	if (bRefreshQueued)
	{
		return;
	}
	UWorld* ComponentWorld = GetWorld();
	if (!ComponentWorld)
	{
		return;
	}
	bRefreshQueued = true;
	ComponentWorld->GetTimerManager().SetTimerForNextTick(this, &UBH_QuestDirector::FlushObjectiveRefresh);
}

void UBH_QuestDirector::FlushObjectiveRefresh()
{
	bRefreshQueued = false;
	FBH_QuestObjective Current;
	const bool bHasObjective = GetCurrentObjective(Current);
	const bool bChanged = bForceBroadcast || bHasObjective != bLastHadObjective || (bHasObjective && !AreObjectivesEqual(Current, LastObjective));
	bForceBroadcast = false;
	bLastHadObjective = bHasObjective;
	LastObjective = bHasObjective ? Current : FBH_QuestObjective();
	if (bChanged)
	{
		OnObjectiveChanged.Broadcast();
	}
}

void UBH_QuestDirector::OnRep_MainQuestClass()
{
	ScheduleObjectiveRefresh(true);
}

void UBH_QuestDirector::OnRep_ReportedEvents()
{
	ScheduleObjectiveRefresh(true); // a target that waits for an event may drop out
}

void UBH_QuestDirector::HandleQuestListChanged(const UQuest* /*Quest*/)
{
	ScheduleObjectiveRefresh(false);
}

void UBH_QuestDirector::HandleQuestNewState(UQuest* /*Quest*/, const UQuestState* /*NewState*/)
{
	ScheduleObjectiveRefresh(false);
}

void UBH_QuestDirector::HandleTaskProgress(const UQuest* /*Quest*/, const UNarrativeTask* /*ProgressedTask*/, const UQuestBranch* /*Branch*/, int32 /*OldProgress*/, int32 /*NewProgress*/)
{
	ScheduleObjectiveRefresh(false);
}

void UBH_QuestDirector::HandleTaskCompleted(const UQuest* /*Quest*/, const UNarrativeTask* /*CompletedTask*/, const UQuestBranch* /*Branch*/)
{
	ScheduleObjectiveRefresh(false);
}

void UBH_QuestDirector::HandleQuestEnded(const UQuest* /*Quest*/, const FText& /*Message*/)
{
	ScheduleObjectiveRefresh(false);
}

void UBH_QuestDirector::HandleWorldFlagChanged(FName /*Flag*/, bool /*bIsSet*/)
{
	ScheduleObjectiveRefresh(true); // gate flags change what a marker may show
}

// ============================================================================
// Quest chain (Phase 14A-2b): auto-start, chaining, quest XP (server)
// ============================================================================

TArray<FBH_QuestChainEntry> UBH_QuestDirector::MakeDefaultQuestChain()
{
	struct FDefault
	{
		const TCHAR* Path;
		int32 XP;
	};
	static const FDefault Defaults[] = {
		{ TEXT("/Game/BlackwoodHollow/Quests/Q_AmongTheWrack.Q_AmongTheWrack_C"), 60 },
		{ TEXT("/Game/BlackwoodHollow/Quests/Q_KingsRoad.Q_KingsRoad_C"), 150 },
		{ TEXT("/Game/BlackwoodHollow/Quests/Q_GuardiansOfTheSpan.Q_GuardiansOfTheSpan_C"), 100 },
	};
	TArray<FBH_QuestChainEntry> Chain;
	for (const FDefault& Default : Defaults)
	{
		FBH_QuestChainEntry Entry;
		Entry.QuestClass = TSoftClassPtr<UQuest>(FSoftObjectPath(Default.Path));
		Entry.XPReward = Default.XP;
		Entry.bMain = true;
		Chain.Add(Entry);
	}
	return Chain;
}

int32 UBH_QuestDirector::SelectNextChainIndex(const TArray<bool>& Handled, int32 AfterIndex)
{
	for (int32 Index = FMath::Max(0, AfterIndex + 1); Index < Handled.Num(); ++Index)
	{
		if (!Handled[Index])
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

int32 UBH_QuestDirector::FindChainIndex(const TArray<FBH_QuestChainEntry>& Chain, const FSoftObjectPath& QuestClassPath)
{
	if (QuestClassPath.IsNull())
	{
		return INDEX_NONE;
	}
	for (int32 Index = 0; Index < Chain.Num(); ++Index)
	{
		if (Chain[Index].QuestClass.ToSoftObjectPath() == QuestClassPath)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

bool UBH_QuestDirector::IsAutoStartLevel(const TArray<FName>& Levels, const FString& MapName)
{
	const FString Stripped = FPackageName::GetShortName(UWorld::RemovePIEPrefix(MapName));
	return !Stripped.IsEmpty() && Levels.Contains(FName(*Stripped));
}

void UBH_QuestDirector::TryAutoStartChain()
{
	const AActor* OwnerActor = GetOwner();
	const UWorld* ComponentWorld = GetWorld();
	if (!bAutoStartQuestChain || !OwnerActor || !OwnerActor->HasAuthority() || !ComponentWorld)
	{
		return;
	}
	if (!IsAutoStartLevel(AutoStartLevels, ComponentWorld->GetMapName()))
	{
		return;
	}
	BeginNextChainEntry(-1);
}

void UBH_QuestDirector::BeginNextChainEntry(int32 AfterIndex)
{
	const AActor* OwnerActor = GetOwner();
	UBH_PartyComponent* Party = GetParty();
	if (!bAutoStartQuestChain || !OwnerActor || !OwnerActor->HasAuthority() || !Party)
	{
		return;
	}

	// Handled = the party already started / finished it (only classes that are loaded can have an instance).
	TArray<bool> Handled;
	Handled.Init(false, QuestChain.Num());
	for (int32 Index = 0; Index < QuestChain.Num(); ++Index)
	{
		if (const UClass* Loaded = QuestChain[Index].QuestClass.Get())
		{
			Handled[Index] = Party->GetQuestInstance(const_cast<UClass*>(Loaded)) != nullptr;
		}
	}
	// One chain quest at a time: if one is still running (a restart / a reload of the level), leave it alone.
	for (const UQuest* Running : Party->GetInProgressQuests())
	{
		if (Running && FindChainIndex(QuestChain, FSoftObjectPath(Running->GetClass())) != INDEX_NONE)
		{
			return;
		}
	}

	int32 Next = SelectNextChainIndex(Handled, AfterIndex);
	while (Next != INDEX_NONE)
	{
		const FBH_QuestChainEntry& Entry = QuestChain[Next];
		TSubclassOf<UQuest> LoadedClass = Entry.QuestClass.IsNull() ? nullptr : Entry.QuestClass.LoadSynchronous();
		if (!LoadedClass)
		{
			UE_LOG(LogBHQuestDirector, Warning, TEXT("Quest chain: entry %d (%s) could not be loaded (asset not created yet?), skipping it."), Next, *Entry.QuestClass.ToString());
			Handled[Next] = true;
			Next = SelectNextChainIndex(Handled, Next);
			continue;
		}
		UE_LOG(LogBHQuestDirector, Log, TEXT("Quest chain: beginning entry %d (%s)."), Next, *GetNameSafe(LoadedClass.Get()));
		BeginPartyQuest(LoadedClass, Entry.bMain);
		return;
	}
	UE_LOG(LogBHQuestDirector, Log, TEXT("Quest chain: nothing left to begin after entry %d."), AfterIndex);
}

void UBH_QuestDirector::GrantChainXP(int32 Amount)
{
	if (Amount <= 0)
	{
		return;
	}
	// Same path as kill XP: UBH_ProgressionComponent::AddXP on each member's progression (the XP toast listens there).
	for (APlayerState* MemberState : UBH_PartyLibrary::GetPartyPlayerStates(this))
	{
		if (UBH_ProgressionComponent* Progression = UBH_ProgressionComponent::FindProgression(MemberState))
		{
			Progression->AddXP(Amount);
		}
	}
}

void UBH_QuestDirector::HandleQuestSucceeded(const UQuest* Quest, const FText& /*Message*/)
{
	ScheduleObjectiveRefresh(false);

	const AActor* OwnerActor = GetOwner();
	if (!bAutoStartQuestChain || !Quest || !OwnerActor || !OwnerActor->HasAuthority())
	{
		return; // clients replay this delegate from the replicated history: only the server grants and chains
	}
	const int32 Index = FindChainIndex(QuestChain, FSoftObjectPath(Quest->GetClass()));
	if (Index == INDEX_NONE)
	{
		return;
	}
	bool bAlreadyGranted = false;
	ChainXPGranted.Add(Quest->GetClass(), &bAlreadyGranted);
	if (bAlreadyGranted)
	{
		return;
	}
	UE_LOG(LogBHQuestDirector, Log, TEXT("Quest chain: %s succeeded, +%d XP to the party."), *GetNameSafe(Quest->GetClass()), QuestChain[Index].XPReward);
	GrantChainXP(QuestChain[Index].XPReward);

	// Begin the next one on the next tick, outside Narrative's success callback.
	if (UWorld* ComponentWorld = GetWorld())
	{
		ComponentWorld->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this, Index]()
		{
			BeginNextChainEntry(Index);
		}));
	}
}

bool UBH_QuestDirector::DebugSkipCurrentQuest()
{
	const AActor* OwnerActor = GetOwner();
	UBH_PartyComponent* Party = GetParty();
	if (!OwnerActor || !OwnerActor->HasAuthority() || !Party)
	{
		return false;
	}
	UQuest* Target = nullptr;
	for (UQuest* Running : Party->GetInProgressQuests())
	{
		if (Running && FindChainIndex(QuestChain, FSoftObjectPath(Running->GetClass())) != INDEX_NONE)
		{
			Target = Running;
			break;
		}
	}
	if (!Target)
	{
		Target = PickTrackedQuest(*Party);
	}
	if (!Target)
	{
		return false;
	}
	UE_LOG(LogBHQuestDirector, Log, TEXT("bh.Quest.Skip: succeeding %s."), *GetNameSafe(Target->GetClass()));
	// UQuest::SucceedQuest is protected in C++ but BlueprintCallable, so call it through reflection (debug path only).
	UFunction* SucceedFunc = Target->FindFunction(TEXT("SucceedQuest"));
	if (!SucceedFunc)
	{
		UE_LOG(LogBHQuestDirector, Warning, TEXT("bh.Quest.Skip: UQuest::SucceedQuest not found."));
		return false;
	}
	struct FSucceedQuestParams
	{
		FText QuestSucceededMessage;
	};
	FSucceedQuestParams SucceedParams;
	SucceedParams.QuestSucceededMessage = NSLOCTEXT("BlackwoodHollow", "QuestSkipped", "Quest skipped (debug).");
	Target->ProcessEvent(SucceedFunc, &SucceedParams);
	return true;
}

// ============================================================================
// Console commands (server only; testing)
// ============================================================================

namespace
{
	UBH_QuestDirector* ResolveServerQuestDirector(UWorld* World, const TCHAR* CommandName)
	{
		if (!World || World->GetNetMode() == NM_Client)
		{
			UE_LOG(LogBHQuestDirector, Warning, TEXT("%s: server only (run it on the host / in standalone)."), CommandName);
			return nullptr;
		}
		UBH_QuestDirector* Director = UBH_QuestDirector::Get(World);
		if (!Director)
		{
			UE_LOG(LogBHQuestDirector, Warning, TEXT("%s: no game state / quest director in this world."), CommandName);
		}
		return Director;
	}

	static FAutoConsoleCommandWithWorldAndArgs CmdQuestEvent(
		TEXT("bh.Quest.Event"),
		TEXT("bh.Quest.Event <TaskName> <Argument> [Quantity] - server: reports one quest event (placeholder testing). Quote an argument with spaces."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UBH_QuestDirector* Director = ResolveServerQuestDirector(World, TEXT("bh.Quest.Event"));
			if (!Director)
			{
				return;
			}
			if (Args.Num() < 2)
			{
				UE_LOG(LogBHQuestDirector, Warning, TEXT("Usage: bh.Quest.Event <TaskName> <Argument> [Quantity]"));
				return;
			}
			const int32 Quantity = Args.Num() > 2 ? FCString::Atoi(*Args[2]) : 1;
			if (Quantity <= 0)
			{
				UE_LOG(LogBHQuestDirector, Warning, TEXT("bh.Quest.Event: Quantity must be 1 or more."));
				return;
			}
			Director->ReportEvent(FName(*Args[0]), Args[1], Quantity);
		}));

	static FAutoConsoleCommandWithWorld CmdQuestSkip(
		TEXT("bh.Quest.Skip"),
		TEXT("bh.Quest.Skip - server: succeeds the whole current chain quest (XP and the next chain quest follow as in normal play)."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (UBH_QuestDirector* Director = ResolveServerQuestDirector(World, TEXT("bh.Quest.Skip")))
			{
				if (!Director->DebugSkipCurrentQuest())
				{
					UE_LOG(LogBHQuestDirector, Warning, TEXT("bh.Quest.Skip: no quest in progress."));
				}
			}
		}));
}
