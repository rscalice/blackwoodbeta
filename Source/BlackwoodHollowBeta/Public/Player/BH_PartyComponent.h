// Blackwood Hollow - the party component (Phase 11P party foundation)
// Target: Unreal Engine 5.8 (C++), Narrative plugin
//
// UNarrativePartyComponent with two small additions for runtime membership churn (players joining / leaving a listen-server session):
//   * RemovePlayerStateMember: removes a member by PlayerState even when its controller is already gone (the stock RemovePartyMember
//     needs a live Member->GetOwningController() and silently does nothing otherwise, leaving a dead entry behind),
//   * PurgeInvalidMembers: drops entries whose component / PlayerState no longer exists.
// It lives on ABH_GameState (replicates to everybody; GetPartyMemberStates is the replicated list, the leader is index 0).

#pragma once

#include "CoreMinimal.h"
#include "NarrativePartyComponent.h"
#include "BH_PartyComponent.generated.h"

class APlayerState;

UCLASS(ClassGroup = (BlackwoodHollow), meta = (BlueprintSpawnableComponent))
class BLACKWOODHOLLOWBETA_API UBH_PartyComponent : public UNarrativePartyComponent
{
	GENERATED_BODY()

public:
	UBH_PartyComponent();

	/** SERVER. Removes the member that belongs to PlayerState (also when its controller is already gone). @return true if an entry was removed. */
	bool RemovePlayerStateMember(APlayerState* PlayerState);

	/** SERVER. Drops members whose Narrative component or PlayerState was destroyed. */
	void PurgeInvalidMembers();

	/** True if PlayerState is currently in the party (any machine: reads the replicated state list). */
	bool ContainsPlayerState(const APlayerState* PlayerState) const;
};
