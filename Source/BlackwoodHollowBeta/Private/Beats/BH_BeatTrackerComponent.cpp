// Blackwood Hollow - party beat tracker (Phase 13B+, implementation)

#include "Beats/BH_BeatTrackerComponent.h"
#include "Beats/BH_BeatTags.h"
#include "Beats/BH_BeatTarget.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
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
	DOREPLIFETIME(UBH_BeatTrackerComponent, PartnerPings);
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

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		if (!CurrentBeat.IsValid() && Rows.Num() > 0)
		{
			SetCurrentBeatInternal(Rows[0].BeatId);
		}
		CompleteBeatsForSetFlags();
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(PartnerTimer, this, &UBH_BeatTrackerComponent::UpdatePartnerPings, PartnerUpdateInterval, true);
		}
	}
}

void UBH_BeatTrackerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bBoundFlags)
	{
		if (ABH_GameState* GameState = Cast<ABH_GameState>(GetOwner()))
		{
			GameState->OnWorldFlagChanged.RemoveDynamic(this, &UBH_BeatTrackerComponent::HandleWorldFlagChanged);
		}
		bBoundFlags = false;
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PartnerTimer);
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

	const ABH_GameState* GameState = GetBeatGameState();
	Out.bGateOpen = Row.RequiredGateFlag.IsNone() || (GameState && GameState->HasWorldFlag(Row.RequiredGateFlag));
	if (const AActor* Target = ResolveTarget(Row))
	{
		Out.bHasTarget = true;
		Out.TargetLocation = Target->GetActorLocation();
	}
	return true;
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

bool UBH_BeatTrackerComponent::SamePings(const TArray<FBH_PartnerPing>& Other) const
{
	if (Other.Num() != PartnerPings.Num())
	{
		return false;
	}
	for (int32 Index = 0; Index < Other.Num(); ++Index)
	{
		const FBH_PartnerPing& A = PartnerPings[Index];
		const FBH_PartnerPing& B = Other[Index];
		if (A.Player != B.Player || A.DeathPhase != B.DeathPhase || FVector::DistSquared(FVector(A.Location), FVector(B.Location)) > FMath::Square(50.0))
		{
			return false;
		}
	}
	return true;
}

void UBH_BeatTrackerComponent::UpdatePartnerPings()
{
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	if (!GameState)
	{
		return;
	}
	TArray<FBH_PartnerPing> Fresh;
	for (APlayerState* PlayerState : GameState->PlayerArray)
	{
		if (!PlayerState)
		{
			continue;
		}
		FBH_PartnerPing Ping;
		Ping.Player = PlayerState;
		if (const APawn* Pawn = PlayerState->GetPawn())
		{
			Ping.Location = Pawn->GetActorLocation();
			const UBH_PlayerDeathComponent* Death = UBH_PlayerDeathComponent::Find(Pawn);
			Ping.DeathPhase = Death ? static_cast<uint8>(Death->GetPhase()) : 0;
		}
		else
		{
			// No pawn right now (respawning): keep the last known entry rather than dropping the marker.
			const FBH_PartnerPing* Last = PartnerPings.FindByPredicate([PlayerState](const FBH_PartnerPing& Existing) { return Existing.Player == PlayerState; });
			if (!Last)
			{
				continue;
			}
			Ping = *Last;
		}
		Fresh.Add(Ping);
	}
	if (!SamePings(Fresh))
	{
		PartnerPings = MoveTemp(Fresh);
		OnPartnersChanged.Broadcast();
	}
}

void UBH_BeatTrackerComponent::OnRep_PartnerPings()
{
	OnPartnersChanged.Broadcast();
}