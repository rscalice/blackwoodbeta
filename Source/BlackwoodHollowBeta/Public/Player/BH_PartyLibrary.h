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
#include "Player/BH_PartyStatus.h"
#include "BH_PartyLibrary.generated.h"

class AActor;
class APawn;
class APlayerController;
class APlayerState;
class ABH_PlayerState;

// Config (DefaultGame.ini, [/Script/BlackwoodHollowBeta.BH_PartyLibrary]): PingGoodBelowMs, PingFairBelowMs, DisconnectedDisplaySeconds.
UCLASS(config = Game)
class BLACKWOODHOLLOWBETA_API UBH_PartyLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Ping below this (ms) is Good. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "BH|Party", meta = (ClampMin = "0.0", ForceUnits = "ms"))
	float PingGoodBelowMs = 80.f;

	/** Ping below this (ms) is Fair; anything else is Poor. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "BH|Party", meta = (ClampMin = "0.0", ForceUnits = "ms"))
	float PingFairBelowMs = 150.f;

	/** How long the partner panel shows "Disconnected" after a partner leaves (read it from the library defaults). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "BH|Party", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float DisconnectedDisplaySeconds = 3.f;

	/** 13I-1. The party members except LocalPC's own player, as ABH_PlayerState, in stable slot order (slot 1..4; unassigned slots last). Falls back to every player state when the party list is empty. */
	UFUNCTION(BlueprintPure, Category = "BH|Party", meta = (WorldContext = "WorldContext"))
	static TArray<ABH_PlayerState*> GetPartnerPlayerStates(const UObject* WorldContext, APlayerController* LocalPC);

	/** 13I-1. Metres between LocalPC's pawn and Partner's pawn. -1 when either pawn does not exist on this machine (e.g. the partner is beyond net cull distance). */
	UFUNCTION(BlueprintPure, Category = "BH|Party")
	static float GetPartnerDistanceMeters(APlayerController* LocalPC, APlayerState* Partner);

	/** 13I-1. Good / Fair / Poor from the player state's ping, using PingGoodBelowMs / PingFairBelowMs. */
	UFUNCTION(BlueprintPure, Category = "BH|Party")
	static EBH_PingQuality GetPingQuality(const APlayerState* PlayerState);

	/** 13I-1. Pure mapping: PingMs below GoodBelowMs = Good, below FairBelowMs = Fair, else Poor. */
	UFUNCTION(BlueprintPure, Category = "BH|Party")
	static EBH_PingQuality ClassifyPing(float PingMs, float GoodBelowMs, float FairBelowMs);

	/** 13I-1. Pure mapping: BlightBuildup 0..100 to stacks 0..10 (divide by 10, round down, clamped). */
	UFUNCTION(BlueprintPure, Category = "BH|Party")
	static int32 BlightBuildupToStacks(float BlightBuildup);

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
