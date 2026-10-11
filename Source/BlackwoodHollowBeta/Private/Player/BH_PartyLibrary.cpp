// Blackwood Hollow - party access helpers (implementation)

#include "Player/BH_PartyLibrary.h"
#include "Player/BH_GameState.h"
#include "Player/BH_PartyComponent.h"
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
