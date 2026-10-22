// Blackwood Hollow - party status component (Phase 14A-3 prep, implementation)

#include "Party/BH_PartyStatusComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "Player/BH_GameState.h"
#include "Player/BH_PlayerDeathComponent.h"
#include "TimerManager.h"

UBH_PartyStatusComponent::UBH_PartyStatusComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UBH_PartyStatusComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UBH_PartyStatusComponent, PartnerPings);
}

UBH_PartyStatusComponent* UBH_PartyStatusComponent::Get(const UObject* WorldContext)
{
	const ABH_GameState* GameState = ABH_GameState::Get(WorldContext);
	return GameState ? GameState->FindComponentByClass<UBH_PartyStatusComponent>() : nullptr;
}

void UBH_PartyStatusComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(PartnerTimer, this, &UBH_PartyStatusComponent::UpdatePartnerPings, PartnerUpdateInterval, true);
		}
	}
}

void UBH_PartyStatusComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PartnerTimer);
	}
	Super::EndPlay(EndPlayReason);
}

bool UBH_PartyStatusComponent::ArePingListsEqual(const TArray<FBH_PartnerPing>& A, const TArray<FBH_PartnerPing>& B)
{
	if (A.Num() != B.Num())
	{
		return false;
	}
	for (int32 Index = 0; Index < A.Num(); ++Index)
	{
		if (A[Index].Player != B[Index].Player || A[Index].DeathPhase != B[Index].DeathPhase
			|| FVector::DistSquared(FVector(A[Index].Location), FVector(B[Index].Location)) > FMath::Square(50.0))
		{
			return false;
		}
	}
	return true;
}

void UBH_PartyStatusComponent::UpdatePartnerPings()
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
	if (!ArePingListsEqual(PartnerPings, Fresh))
	{
		PartnerPings = MoveTemp(Fresh);
		OnPartnersChanged.Broadcast();
	}
}

void UBH_PartyStatusComponent::OnRep_PartnerPings()
{
	OnPartnersChanged.Broadcast();
}
