// Blackwood Hollow - party access helpers (implementation)

#include "Player/BH_PartyLibrary.h"
#include "Player/BH_GameState.h"
#include "Player/BH_PartyComponent.h"
#include "Player/BH_PlayerState.h"
#include "Algo/StableSort.h"
#include "GameFramework/PlayerController.h"
#include "Player/BH_PlayerDeathComponent.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"

TArray<APlayerState*> UBH_PartyLibrary::GetPartyPlayerStates(const UObject* WorldContext)
{
	TArray<APlayerState*> Result;
	const UWorld* ContextWorld = WorldContext ? WorldContext->GetWorld() : nullptr;
	const AGameStateBase* GameStateBase = ContextWorld ? ContextWorld->GetGameState() : nullptr;
	if (!GameStateBase)
	{
		return Result;
	}

	if (const ABH_GameState* PartyGameState = Cast<ABH_GameState>(GameStateBase))
	{
		if (const UBH_PartyComponent* Party = PartyGameState->GetPartyComponent())
		{
			const TArray<APlayerState*> MemberStates = Party->GetPartyMemberStates();
			for (APlayerState* MemberState : MemberStates)
			{
				if (IsValid(MemberState))
				{
					Result.AddUnique(MemberState);
				}
			}
		}
	}

	if (Result.IsEmpty())
	{
		// No party game state / nobody registered yet: everybody playing counts.
		for (APlayerState* AnyState : GameStateBase->PlayerArray)
		{
			if (IsValid(AnyState) && !AnyState->IsOnlyASpectator())
			{
				Result.AddUnique(AnyState);
			}
		}
	}
	return Result;
}

TArray<APawn*> UBH_PartyLibrary::GetLivingPartyPawns(const UObject* WorldContext)
{
	TArray<APawn*> Result;
	ForEachPartyMember(WorldContext, [&Result](APlayerState&, APawn* MemberPawn)
	{
		if (MemberPawn && UBH_PlayerDeathComponent::IsLivingPlayer(MemberPawn))
		{
			Result.Add(MemberPawn);
		}
	});
	return Result;
}

APlayerState* UBH_PartyLibrary::ResolvePlayerState(const AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}
	if (const APlayerState* AsState = Cast<APlayerState>(Actor))
	{
		return const_cast<APlayerState*>(AsState);
	}
	if (const APawn* AsPawn = Cast<APawn>(Actor))
	{
		return AsPawn->GetPlayerState();
	}
	if (const AController* AsController = Cast<AController>(Actor))
	{
		return AsController->PlayerState;
	}
	return nullptr;
}

bool UBH_PartyLibrary::IsInSameParty(const UObject* WorldContext, const AActor* ActorA, const AActor* ActorB)
{
	APlayerState* StateA = ResolvePlayerState(ActorA);
	APlayerState* StateB = ResolvePlayerState(ActorB);
	if (!StateA || !StateB)
	{
		return false;
	}
	if (StateA == StateB)
	{
		return true;
	}
	const TArray<APlayerState*> PartyStates = GetPartyPlayerStates(WorldContext);
	return PartyStates.Contains(StateA) && PartyStates.Contains(StateB);
}

void UBH_PartyLibrary::ForEachPartyMember(const UObject* WorldContext, TFunctionRef<void(APlayerState&, APawn*)> Fn)
{
	const TArray<APlayerState*> PartyStates = GetPartyPlayerStates(WorldContext);
	for (APlayerState* MemberState : PartyStates)
	{
		if (MemberState)
		{
			Fn(*MemberState, MemberState->GetPawn());
		}
	}
}

// ============================================================================
// Partner panel helpers (13I-1)
// ============================================================================

TArray<ABH_PlayerState*> UBH_PartyLibrary::GetPartnerPlayerStates(const UObject* WorldContext, APlayerController* LocalPC)
{
	const APlayerState* LocalState = LocalPC ? LocalPC->PlayerState.Get() : nullptr;

	TArray<ABH_PlayerState*> Partners;
	for (APlayerState* MemberState : GetPartyPlayerStates(WorldContext))
	{
		ABH_PlayerState* BHState = Cast<ABH_PlayerState>(MemberState);
		if (BHState && BHState != LocalState && !BHState->IsInactive())
		{
			Partners.Add(BHState);
		}
	}

	// Stable order by party slot (unassigned slot 0 sorts last), ties keep the party order.
	Algo::StableSort(Partners, [](const ABH_PlayerState* Lhs, const ABH_PlayerState* Rhs)
	{
		const int32 LhsKey = Lhs->GetPlayerSlot() > 0 ? Lhs->GetPlayerSlot() : MAX_int32;
		const int32 RhsKey = Rhs->GetPlayerSlot() > 0 ? Rhs->GetPlayerSlot() : MAX_int32;
		return LhsKey < RhsKey;
	});
	return Partners;
}

float UBH_PartyLibrary::GetPartnerDistanceMeters(APlayerController* LocalPC, APlayerState* Partner)
{
	const APawn* LocalBody = LocalPC ? LocalPC->GetPawn() : nullptr;
	const APawn* PartnerBody = Partner ? Partner->GetPawn() : nullptr;
	if (!LocalBody || !PartnerBody)
	{
		return -1.f;
	}
	return FVector::Dist(LocalBody->GetActorLocation(), PartnerBody->GetActorLocation()) / 100.f;
}

EBH_PingQuality UBH_PartyLibrary::ClassifyPing(float PingMs, float GoodBelowMs, float FairBelowMs)
{
	if (PingMs < GoodBelowMs)
	{
		return EBH_PingQuality::Good;
	}
	return PingMs < FairBelowMs ? EBH_PingQuality::Fair : EBH_PingQuality::Poor;
}

EBH_PingQuality UBH_PartyLibrary::GetPingQuality(const APlayerState* PlayerState)
{
	const UBH_PartyLibrary* Settings = GetDefault<UBH_PartyLibrary>();
	const float PingMs = PlayerState ? PlayerState->GetPingInMilliseconds() : 0.f;
	return ClassifyPing(PingMs, Settings->PingGoodBelowMs, Settings->PingFairBelowMs);
}

int32 UBH_PartyLibrary::BlightBuildupToStacks(float BlightBuildup)
{
	return FMath::Clamp(FMath::FloorToInt(BlightBuildup / 10.f), 0, 10);
}
