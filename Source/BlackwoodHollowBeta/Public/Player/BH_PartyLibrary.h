// Blackwood Hollow - party access helpers (Phase 11P party foundation)
// Target: Unreal Engine 5.8 (C++)
//
// Gameplay code asks the party through this library instead of iterating every pawn / player controller in the world.
// The party is ABH_GameState's UBH_PartyComponent (replicated member list, host first). When the world has no ABH_GameState, or the party
// has no registered members yet (a player joining: registration is deferred a few frames), every player state in the world counts as the
// party, so other maps and the first moments of a session keep working. Works on any machine; living-pawn queries need the pawns to exist
// locally (server: all of them, clients: relevant ones).

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BH_PartyLibrary.generated.h"

class AActor;
class APawn;
class APlayerState;

UCLASS()
class BLACKWOODHOLLOWBETA_API UBH_PartyLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** The player states of the party, leader first. Falls back to all player states in the world. */
	UFUNCTION(BlueprintPure, Category = "BH|Party", meta = (WorldContext = "WorldContext"))
	static TArray<APlayerState*> GetPartyPlayerStates(const UObject* WorldContext);

	/** The pawns of party members that exist and are alive (not dead / downed / ragdolled out, see UBH_PlayerDeathComponent::IsLivingPlayer). */
	UFUNCTION(BlueprintPure, Category = "BH|Party", meta = (WorldContext = "WorldContext"))
	static TArray<APawn*> GetLivingPartyPawns(const UObject* WorldContext);

	/** True when both actors (pawn, controller or player state) belong to the same party. False if either is not a player. */
	UFUNCTION(BlueprintPure, Category = "BH|Party", meta = (WorldContext = "WorldContext"))
	static bool IsInSameParty(const UObject* WorldContext, const AActor* ActorA, const AActor* ActorB);

	/** C++ helper: Fn(PlayerState, Pawn) for each party member in order (leader first). Pawn is null while the member has none. */
	static void ForEachPartyMember(const UObject* WorldContext, TFunctionRef<void(APlayerState&, APawn*)> Fn);

private:
	/** Resolves a pawn / controller / player state to its player state (null for anything else). */
	static APlayerState* ResolvePlayerState(const AActor* Actor);
};
