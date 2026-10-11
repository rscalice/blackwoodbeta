// Blackwood Hollow - game state: the party (implementation)

#include "Player/BH_GameState.h"
#include "Player/BH_PartyComponent.h"
#include "NarrativeComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogBHParty, Log, All);

ABH_GameState::ABH_GameState()
{
	// AGameStateBase is replicated and always relevant, so the party component (and its replicated member list) reaches every client.
	PartyComponent = CreateDefaultSubobject<UBH_PartyComponent>(TEXT("PartyComponent"));
}

void ABH_GameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABH_GameState, bPartyInCombat);
	DOREPLIFETIME(ABH_GameState, WorldFlags);
}

ABH_GameState* ABH_GameState::Get(const UObject* WorldContext)
{
	const UWorld* ContextWorld = WorldContext ? WorldContext->GetWorld() : nullptr;
	return ContextWorld ? ContextWorld->GetGameState<ABH_GameState>() : nullptr;
}

void ABH_GameState::SetWorldFlag(FName Flag, bool bValue)
{
	if (!HasAuthority() || Flag.IsNone())
	{
		return;
	}
	const bool bHad = WorldFlags.Contains(Flag);
	if (bHad == bValue)
	{
		return;
	}
	if (bValue)
	{
		WorldFlags.Add(Flag);
	}
	else
	{
		WorldFlags.Remove(Flag);
	}
	ForceNetUpdate();
	LastBroadcastFlags = WorldFlags;
	OnWorldFlagChanged.Broadcast(Flag, bValue); // the server does not get the RepNotify
}

void ABH_GameState::OnRep_WorldFlags()
{
	// Turn the replicated array into one event per changed flag.
	const TArray<FName> Previous = LastBroadcastFlags;
	LastBroadcastFlags = WorldFlags;
	for (const FName& Flag : WorldFlags)
	{
		if (!Previous.Contains(Flag))
		{
			OnWorldFlagChanged.Broadcast(Flag, true);
		}
	}
	for (const FName& Flag : Previous)
	{
		if (!WorldFlags.Contains(Flag))
		{
			OnWorldFlagChanged.Broadcast(Flag, false);
		}
	}
}

void ABH_GameState::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		// Players that joined before this game state began play (or whose AddPlayerState we missed).
		for (APlayerState* ExistingState : PlayerArray)
		{
			if (IsValid(ExistingState))
			{
				AddPlayerState(ExistingState);
			}
		}
	}
}

void ABH_GameState::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (const UWorld* GameWorld = GetWorld())
	{
		GameWorld->GetTimerManager().ClearTimer(RegistrationTimer);
	}
	PendingRegistrations.Reset();
	Super::EndPlay(EndPlayReason);
}

void ABH_GameState::SetPartyInCombat(bool bInCombat)
{
	if (HasAuthority() && bPartyInCombat != bInCombat)
	{
		bPartyInCombat = bInCombat;
		ForceNetUpdate();
	}
}

APlayerState* ABH_GameState::GetPartyLeaderState() const
{
	if (!PartyComponent)
	{
		return nullptr;
	}
	const TArray<APlayerState*> States = PartyComponent->GetPartyMemberStates();
	for (APlayerState* MemberState : States)
	{
		if (IsValid(MemberState))
		{
			return MemberState;
		}
	}
	return nullptr;
}

// ============================================================================
// Membership (server)
// ============================================================================

void ABH_GameState::AddPlayerState(APlayerState* PlayerState)
{
	Super::AddPlayerState(PlayerState);

	if (!HasAuthority() || !PlayerState || !PartyComponent)
	{
		return;
	}
	if (PartyComponent->ContainsPlayerState(PlayerState))
	{
		return;
	}
	for (const FPendingRegistration& Pending : PendingRegistrations)
	{
		if (Pending.State.Get() == PlayerState)
		{
			return;
		}
	}

	FPendingRegistration NewPending;
	NewPending.State = PlayerState;
	NewPending.Attempts = 0;
	PendingRegistrations.Add(NewPending);
	ProcessPendingRegistrations();
}

void ABH_GameState::RemovePlayerState(APlayerState* PlayerState)
{
	Super::RemovePlayerState(PlayerState);

	if (!HasAuthority() || !PlayerState)
	{
		return;
	}

	PendingRegistrations.RemoveAll([PlayerState](const FPendingRegistration& Pending)
	{
		return !Pending.State.IsValid() || Pending.State.Get() == PlayerState;
	});

	if (PartyComponent && PartyComponent->RemovePlayerStateMember(PlayerState))
	{
		UE_LOG(LogBHParty, Log, TEXT("Party: %s left (%d member(s) remain)."), *GetNameSafe(PlayerState), PartyComponent->GetPartyMemberStates().Num());
	}
}

void ABH_GameState::ProcessPendingRegistrations()
{
	// In order, so the first player state added (the host) is registered first and becomes the leader. Stops at the first one that is not ready.
	while (PendingRegistrations.Num() > 0)
	{
		FPendingRegistration& Head = PendingRegistrations[0];
		APlayerState* HeadState = Head.State.Get();
		bool bDone = (HeadState == nullptr);
		if (HeadState)
		{
			bDone = TryRegisterPlayerState(HeadState);
			if (!bDone)
			{
				++Head.Attempts;
				if (Head.Attempts >= MaxRegistrationRetries)
				{
					UE_LOG(LogBHParty, Warning, TEXT("Party: gave up registering %s (no matching controller after %d retries)."), *GetNameSafe(HeadState), Head.Attempts);
					bDone = true;
				}
			}
		}
		if (!bDone)
		{
			break;
		}
		PendingRegistrations.RemoveAt(0);
	}

	if (const UWorld* GameWorld = GetWorld())
	{
		FTimerManager& TimerManager = GameWorld->GetTimerManager();
		if (PendingRegistrations.Num() > 0)
		{
			TimerManager.SetTimer(RegistrationTimer, this, &ABH_GameState::ProcessPendingRegistrations, FMath::Max(0.02f, RegistrationRetryInterval), false);
		}
		else
		{
			TimerManager.ClearTimer(RegistrationTimer);
		}
	}
}

bool ABH_GameState::TryRegisterPlayerState(APlayerState* PlayerState)
{
	if (!PlayerState || !PartyComponent)
	{
		return true;
	}
	if (PlayerState->IsInactive() || PlayerState->IsOnlyASpectator())
	{
		return true; // not a playing member
	}

	AActor* StateOwner = PlayerState->GetOwner();
	if (!StateOwner)
	{
		return false; // owner not assigned yet
	}
	APlayerController* Controller = Cast<APlayerController>(StateOwner);
	if (!Controller)
	{
		return true; // an AI controller's state: never a party member
	}
	if (Controller->PlayerState != PlayerState)
	{
		return false; // the controller does not point at its state yet: retry shortly
	}
	if (PartyComponent->ContainsPlayerState(PlayerState))
	{
		return true;
	}

	UNarrativeComponent* Member = FindOrCreateNarrativeComponent(Controller);
	if (!Member)
	{
		UE_LOG(LogBHParty, Warning, TEXT("Party: no Narrative component for %s."), *GetNameSafe(Controller));
		return true;
	}
	if (!PartyComponent->AddPartyMember(Member))
	{
		return false;
	}

	UE_LOG(LogBHParty, Log, TEXT("Party: %s joined (%d member(s), leader %s)."), *GetNameSafe(PlayerState), PartyComponent->GetPartyMemberStates().Num(), *GetNameSafe(GetPartyLeaderState()));
	return true;
}

UNarrativeComponent* ABH_GameState::FindOrCreateNarrativeComponent(APlayerController* Controller) const
{
	if (!Controller)
	{
		return nullptr;
	}
	if (UNarrativeComponent* Existing = Controller->FindComponentByClass<UNarrativeComponent>())
	{
		return Existing;
	}

	UNarrativeComponent* Created = NewObject<UNarrativeComponent>(Controller, UNarrativeComponent::StaticClass(), TEXT("BH_NarrativeComponent"));
	if (Created)
	{
		Created->SetIsReplicated(true);
		Controller->AddInstanceComponent(Created);
		Created->RegisterComponent();
	}
	return Created;
}
