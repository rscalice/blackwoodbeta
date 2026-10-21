
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
	Party->OnQuestSucceeded.AddUniqueDynamic(this, &UBH_QuestDirector::HandleQuestEnded);
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
		Party->OnQuestSucceeded.RemoveDynamic(this, &UBH_QuestDirector::HandleQuestEnded);
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
