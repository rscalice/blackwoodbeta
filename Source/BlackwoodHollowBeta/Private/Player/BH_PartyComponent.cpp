// Blackwood Hollow - the party component (implementation)

#include "Player/BH_PartyComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"

UBH_PartyComponent::UBH_PartyComponent()
{
	// The base class constructor is protected; a derived constructor may call it. Replication is on by default in UNarrativeComponent.
}

bool UBH_PartyComponent::ContainsPlayerState(const APlayerState* PlayerState) const
{
	return PlayerState && PartyMemberStates.Contains(PlayerState);
}

bool UBH_PartyComponent::RemovePlayerStateMember(APlayerState* PlayerState)
{
	const AActor* OwnerActor = GetOwner();
	if (!PlayerState || !OwnerActor || !OwnerActor->HasAuthority())
	{
		return false;
	}

	bool bRemoved = false;
	for (int32 Index = PartyMembers.Num() - 1; Index >= 0; --Index)
	{
		UNarrativeComponent* Member = PartyMembers[Index];
		APlayerController* MemberController = IsValid(Member) ? Member->GetOwningController() : nullptr;
		const bool bBelongsToState = MemberController && MemberController->PlayerState == PlayerState;
		const bool bOrphan = !MemberController; // destroyed component or controller: nothing left to keep
		if (!bBelongsToState && !bOrphan)
		{
			continue;
		}
		if (bBelongsToState && RemovePartyMember(Member))
		{
			bRemoved = true;
			continue;
		}
		if (PartyMembers.IsValidIndex(Index) && PartyMembers[Index] == Member)
		{
			PartyMembers.RemoveAt(Index);
			bRemoved = true;
		}
	}

	if (PartyMemberStates.Remove(PlayerState) > 0)
	{
		bRemoved = true;
	}
	PurgeInvalidMembers();
	return bRemoved;
}

void UBH_PartyComponent::PurgeInvalidMembers()
{
	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority())
	{
		return;
	}
	PartyMembers.RemoveAll([](const UNarrativeComponent* Member)
	{
		return !IsValid(Member);
	});
	PartyMemberStates.RemoveAll([](const APlayerState* State)
	{
		return !IsValid(State);
	});
}
