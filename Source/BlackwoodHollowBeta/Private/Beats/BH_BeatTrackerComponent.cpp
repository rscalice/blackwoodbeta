// Blackwood Hollow - party beat tracker (Phase 13B+, implementation)

#include "Beats/BH_BeatTrackerComponent.h"
#include "Beats/BH_BeatTags.h"
#include "Beats/BH_BeatTarget.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Loot/BH_LootLibrary.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "Player/BH_GameState.h"
#include "Player/BH_PlayerDeathComponent.h"
#include "TimerManager.h"

UBH_BeatTrackerComponent::UBH_BeatTrackerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	BeatTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/BlackwoodHollow/Data/DT_BH_Beats.DT_BH_Beats")));
}

void UBH_BeatTrackerComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UBH_BeatTrackerComponent, CurrentBeat);
	DOREPLIFETIME(UBH_BeatTrackerComponent, ChecklistDone);
}

UBH_BeatTrackerComponent* UBH_BeatTrackerComponent::Get(const UObject* WorldContext)
{
	const ABH_GameState* GameState = ABH_GameState::Get(WorldContext);
	return GameState ? GameState->FindComponentByClass<UBH_BeatTrackerComponent>() : nullptr;
}

const ABH_GameState* UBH_BeatTrackerComponent::GetBeatGameState() const
{
	return Cast<ABH_GameState>(GetOwner());
}

void UBH_BeatTrackerComponent::LoadRows()
{
	if (bRowsLoaded)
	{
		return;
	}
	Rows.Reset();
	const UDataTable* Table = BeatTable.LoadSynchronous();
	if (!Table)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BH_Beat] Beat table %s did not load; no objectives until DT_BH_Beats exists."), *BeatTable.ToString());
		return;
	}
	for (const FName& RowName : Table->GetRowNames())
	{
		if (const FBH_BeatRow* Row = Table->FindRow<FBH_BeatRow>(RowName, TEXT("BH_BeatTracker")))
		{
			Rows.Add(*Row);
		}
	}
	bRowsLoaded = true;
}

void UBH_BeatTrackerComponent::BeginPlay()
{
	Super::BeginPlay();
	LoadRows();

	if (ABH_GameState* GameState = Cast<ABH_GameState>(GetOwner()))
	{
		GameState->OnWorldFlagChanged.AddDynamic(this, &UBH_BeatTrackerComponent::HandleWorldFlagChanged);
		bBoundFlags = true;
	}

	if (UBH_PartyStatusComponent* Status = GetOwner() ? GetOwner()->FindComponentByClass<UBH_PartyStatusComponent>() : nullptr)
	{
		Status->OnPartnersChanged.AddUniqueDynamic(this, &UBH_BeatTrackerComponent::HandlePartyPartnersChanged);
	}

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		if (!CurrentBeat.IsValid() && Rows.Num() > 0)
		{
			SetCurrentBeatInternal(Rows[0].BeatId);
		}
		CompleteBeatsForSetFlags();
	}
}

void UBH_BeatTrackerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UBH_PartyStatusComponent* Status = GetOwner() ? GetOwner()->FindComponentByClass<UBH_PartyStatusComponent>() : nullptr)
	{
		Status->OnPartnersChanged.RemoveDynamic(this, &UBH_BeatTrackerComponent::HandlePartyPartnersChanged);
	}
	if (bBoundFlags)
	{
		if (ABH_GameState* GameState = Cast<ABH_GameState>(GetOwner()))
		{
			GameState->OnWorldFlagChanged.RemoveDynamic(this, &UBH_BeatTrackerComponent::HandleWorldFlagChanged);
		}
		bBoundFlags = false;
	}
	Super::EndPlay(EndPlayReason);
}

int32 UBH_BeatTrackerComponent::GetBeatIndex(FGameplayTag Beat) const
{
	if (!Beat.IsValid())
	{
		return INDEX_NONE;
	}
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		if (Rows[Index].BeatId == Beat)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

bool UBH_BeatTrackerComponent::CompleteBeat(FGameplayTag Beat)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}
	LoadRows();
	const int32 DoneIndex = GetBeatIndex(Beat);
	if (DoneIndex == INDEX_NONE)
	{
		return false;
	}
	const int32 CurrentIndex = GetBeatIndex(CurrentBeat);
	// Already past this beat (or everything is done): forward only.
	if (CurrentIndex == INDEX_NONE && CurrentBeat.IsValid())
	{
		return false;
	}
	if (CurrentIndex != INDEX_NONE && DoneIndex < CurrentIndex)
	{
		return false;
	}
	const FGameplayTag Next = Rows.IsValidIndex(DoneIndex + 1) ? Rows[DoneIndex + 1].BeatId : TAG_Beat_Island1_Complete.GetTag();
	SetCurrentBeatInternal(Next);
	CompleteBeatsForSetFlags();
	return true;
}

void UBH_BeatTrackerComponent::SetCurrentBeat(FGameplayTag Beat)
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		LoadRows();
		SetCurrentBeatInternal(Beat);
	}
}

bool UBH_BeatTrackerComponent::CompleteBeatOnServer(const UObject* WorldContext, FGameplayTag Beat)
{
	UBH_BeatTrackerComponent* Tracker = Get(WorldContext);
	return Tracker && Tracker->CompleteBeat(Beat);
}

void UBH_BeatTrackerComponent::SetCurrentBeatInternal(FGameplayTag NewBeat)
{
	if (CurrentBeat == NewBeat)
	{
		return;
	}
	const FGameplayTag OldBeat = CurrentBeat;
	CurrentBeat = NewBeat;
	CachedTarget.Reset();
	LastTargetSearchTime = -100.0;
	UE_LOG(LogTemp, Log, TEXT("[BH_Beat] beat %s -> %s"), *OldBeat.ToString(), *NewBeat.ToString());
	// The server has no RepNotify call: broadcast here. Clients get it from OnRep_CurrentBeat.
	OnBeatChanged.Broadcast(NewBeat, OldBeat);
	OnObjectiveChanged.Broadcast();
}

void UBH_BeatTrackerComponent::CompleteBeatsForSetFlags()
{
	const ABH_GameState* GameState = GetBeatGameState();
	if (!GameState)
	{
		return;
	}
	// A beat whose end flag is already set is done. Walk in table order; each completion may reveal the next one that is also done.
	for (int32 Pass = 0; Pass < Rows.Num(); ++Pass)
	{
		bool bAdvanced = false;
		for (const FBH_BeatRow& Row : Rows)
		{
			if (!Row.CompleteWorldFlag.IsNone() && GameState->HasWorldFlag(Row.CompleteWorldFlag) && GetBeatIndex(Row.BeatId) >= GetBeatIndex(CurrentBeat))
			{
				const FGameplayTag Before = CurrentBeat;
				const int32 DoneIndex = GetBeatIndex(Row.BeatId);
				const FGameplayTag Next = Rows.IsValidIndex(DoneIndex + 1) ? Rows[DoneIndex + 1].BeatId : TAG_Beat_Island1_Complete.GetTag();
				SetCurrentBeatInternal(Next);
				bAdvanced = (Before != CurrentBeat);
				break;
			}
		}
		if (!bAdvanced)
		{
			break;
		}
	}
}

void UBH_BeatTrackerComponent::HandleWorldFlagChanged(FName /*Flag*/, bool /*bIsSet*/)
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		CompleteBeatsForSetFlags();
	}
	// Gate flags change what the marker may show.
	OnObjectiveChanged.Broadcast();
}

void UBH_BeatTrackerComponent::OnRep_CurrentBeat(FGameplayTag OldBeat)
{
	CachedTarget.Reset();
	LastTargetSearchTime = -100.0;
	OnBeatChanged.Broadcast(CurrentBeat, OldBeat);
	OnObjectiveChanged.Broadcast();
}

AActor* UBH_BeatTrackerComponent::ResolveTarget(const FBH_BeatRow& Row)
{
	if (AActor* Cached = CachedTarget.Get())
	{
		return Cached;
	}
	if (AActor* Explicit = Row.TargetActor.Get())
	{
		CachedTarget = Explicit;
		return Explicit;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	// A target that has not arrived yet (client) is searched at most once a second.
	if (World->GetTimeSeconds() - LastTargetSearchTime < 1.0)
	{
		return nullptr;
	}
	LastTargetSearchTime = World->GetTimeSeconds();
	const FGameplayTag Wanted = Row.TargetTag.IsValid() ? Row.TargetTag : Row.BeatId;
	for (TActorIterator<ABH_BeatTarget> It(World); It; ++It)
	{
		if (It->BeatTag == Wanted)
		{
			CachedTarget = *It;
			return *It;
		}
	}
	return nullptr;
}

bool UBH_BeatTrackerComponent::GetCurrentObjective(FBH_ObjectiveInfo& Out)
{
	LoadRows();
	Out = FBH_ObjectiveInfo();
	const int32 Index = GetBeatIndex(CurrentBeat);
	if (Index == INDEX_NONE)
	{
		return false;
	}
	const FBH_BeatRow& Row = Rows[Index];
	Out.BeatId = Row.BeatId;
	Out.ObjectiveText = Row.ObjectiveText;
	Out.bShowWorldMarker = Row.bShowWorldMarker;
	int32 ChecklistDoneCount = 0;
	int32 ChecklistTotal = 0;
	if (GetChecklistProgress(GetLocalPlayerState(), ChecklistDoneCount, ChecklistTotal) && ChecklistTotal > 0)
	{
		Out.ObjectiveText = FText::Format(NSLOCTEXT("BlackwoodHollow", "BeatChecklistProgress", "{0} ({1}/{2})"), Row.ObjectiveText, ChecklistDoneCount, ChecklistTotal);
	}

	const ABH_GameState* GameState = GetBeatGameState();
	Out.bGateOpen = Row.RequiredGateFlag.IsNone() || (GameState && GameState->HasWorldFlag(Row.RequiredGateFlag));
	if (const AActor* Target = ResolveTarget(Row))
	{
		Out.bHasTarget = true;
		Out.TargetLocation = Target->GetActorLocation();
	}
	return true;
}

// ============================================================================
// Scavenging checklist
// ============================================================================

namespace
{
	FString ChecklistKey(const APlayerState* Player, FName ItemId)
	{
		return UBH_LootLibrary::GetPlayerKey(Player) + TEXT("|") + ItemId.ToString();
	}
}

FGameplayTag UBH_BeatTrackerComponent::GetCurrentTargetTag()
{
	LoadRows();
	const int32 Index = GetBeatIndex(CurrentBeat);
	if (Index == INDEX_NONE)
	{
		return FGameplayTag();
	}
	const FBH_BeatRow& Row = Rows[Index];
	return Row.TargetTag.IsValid() ? Row.TargetTag : Row.BeatId;
}

const TArray<TWeakObjectPtr<ABH_BeatTarget>>& UBH_BeatTrackerComponent::GetChecklistTargets()
{
	UWorld* World = GetWorld();
	const FGameplayTag Wanted = GetCurrentTargetTag();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	if (World && (ChecklistCacheTag != Wanted || (ChecklistTargets.IsEmpty() && Now - ChecklistCacheTime > 1.0)))
	{
		ChecklistCacheTag = Wanted;
		ChecklistCacheTime = Now;
		ChecklistTargets.Reset();
		if (Wanted.IsValid())
		{
			for (TActorIterator<ABH_BeatTarget> It(World); It; ++It)
			{
				if (It->BeatTag == Wanted && !It->ChecklistItemId.IsNone())
				{
					ChecklistTargets.Add(*It);
				}
			}
		}
	}
	return ChecklistTargets;
}

const APlayerState* UBH_BeatTrackerComponent::GetLocalPlayerState() const
{
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	return PC ? PC->PlayerState.Get() : nullptr;
}

bool UBH_BeatTrackerComponent::IsChecklistItemDone(const APlayerState* Player, FName ItemId) const
{
	return Player && !ItemId.IsNone() && ChecklistDone.Contains(ChecklistKey(Player, ItemId));
}

bool UBH_BeatTrackerComponent::GetChecklistProgress(const APlayerState* Player, int32& OutDone, int32& OutTotal)
{
	OutDone = 0;
	OutTotal = 0;
	TSet<FName> Seen;
	for (const TWeakObjectPtr<ABH_BeatTarget>& Weak : GetChecklistTargets())
	{
		const ABH_BeatTarget* Target = Weak.Get();
		if (!Target || Seen.Contains(Target->ChecklistItemId))
		{
			continue;
		}
		Seen.Add(Target->ChecklistItemId);
		++OutTotal;
		OutDone += IsChecklistItemDone(Player, Target->ChecklistItemId) ? 1 : 0;
	}
	return OutTotal > 0;
}

bool UBH_BeatTrackerComponent::IsTargetActive(const ABH_BeatTarget* Target)
{
	if (!Target)
	{
		return false;
	}
	const FGameplayTag Wanted = GetCurrentTargetTag();
	if (!Wanted.IsValid() || Target->BeatTag != Wanted)
	{
		return false;
	}
	return Target->ChecklistItemId.IsNone() || !IsChecklistItemDone(GetLocalPlayerState(), Target->ChecklistItemId);
}

void UBH_BeatTrackerComponent::ReportChecklistItem(const UObject* WorldContext, APlayerState* Player, FName ItemId)
{
	UBH_BeatTrackerComponent* Tracker = Get(WorldContext);
	if (!Tracker || !Tracker->GetOwner() || !Tracker->GetOwner()->HasAuthority() || !Player || ItemId.IsNone())
	{
		return;
	}
	const FString Key = ChecklistKey(Player, ItemId);
	if (Tracker->ChecklistDone.Contains(Key))
	{
		return;
	}
	Tracker->ChecklistDone.Add(Key);
	Tracker->GetOwner()->ForceNetUpdate();
	UE_LOG(LogTemp, Log, TEXT("[BH_Beat] checklist: %s did '%s'"), *Player->GetPlayerName(), *ItemId.ToString());
	Tracker->OnObjectiveChanged.Broadcast(); // the server has no RepNotify call
	Tracker->EvaluateChecklistCompletion();
}

void UBH_BeatTrackerComponent::ResetChecklistItem(const UObject* WorldContext, FName ItemId)
{
	UBH_BeatTrackerComponent* Tracker = Get(WorldContext);
	if (!Tracker || !Tracker->GetOwner() || !Tracker->GetOwner()->HasAuthority() || ItemId.IsNone())
	{
		return;
	}
	const FString Suffix = TEXT("|") + ItemId.ToString();
	if (Tracker->ChecklistDone.RemoveAll([&Suffix](const FString& Entry) { return Entry.EndsWith(Suffix); }) > 0)
	{
		Tracker->GetOwner()->ForceNetUpdate();
		Tracker->OnObjectiveChanged.Broadcast();
	}
}

void UBH_BeatTrackerComponent::EvaluateChecklistCompletion()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !GetWorld())
	{
		return;
	}
	TSet<FName> Ids;
	for (const TWeakObjectPtr<ABH_BeatTarget>& Weak : GetChecklistTargets())
	{
		if (const ABH_BeatTarget* Target = Weak.Get())
		{
			Ids.Add(Target->ChecklistItemId);
		}
	}
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	if (Ids.IsEmpty() || !GameState)
	{
		return;
	}
	for (const APlayerState* Player : GameState->PlayerArray)
	{
		if (!Player || Player->IsSpectator() || Player->IsABot())
		{
			continue;
		}
		bool bAll = true;
		for (const FName& Id : Ids)
		{
			bAll &= IsChecklistItemDone(Player, Id);
		}
		if (bAll)
		{
			UE_LOG(LogTemp, Log, TEXT("[BH_Beat] checklist of %s finished by %s"), *CurrentBeat.ToString(), *Player->GetPlayerName());
			CompleteBeat(CurrentBeat);
			return;
		}
	}
}

void UBH_BeatTrackerComponent::OnRep_ChecklistDone()
{
	OnObjectiveChanged.Broadcast();
}

bool UBH_BeatTrackerComponent::ShouldShowWorldMarker(const FVector& ViewerLocation)
{
	FBH_ObjectiveInfo Info;
	if (!GetCurrentObjective(Info) || !Info.bHasTarget || !Info.bGateOpen || !Info.bShowWorldMarker)
	{
		return false;
	}
	return FVector::DistSquared(ViewerLocation, Info.TargetLocation) > FMath::Square(WorldMarkerHideDistance);
}

const TArray<FBH_PartnerPing>& UBH_BeatTrackerComponent::GetPartnerPings() const
{
	// DEPRECATED forwarder (14A-3 prep): the pings live on UBH_PartyStatusComponent.
	static const TArray<FBH_PartnerPing> Empty;
	const UBH_PartyStatusComponent* Status = GetOwner() ? GetOwner()->FindComponentByClass<UBH_PartyStatusComponent>() : nullptr;
	return Status ? Status->GetPartnerPings() : Empty;
}

void UBH_BeatTrackerComponent::HandlePartyPartnersChanged()
{
	OnPartnersChanged.Broadcast();
}