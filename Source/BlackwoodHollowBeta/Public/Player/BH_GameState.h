// Blackwood Hollow - game state: the party (Phase 11P party foundation)
// Target: Unreal Engine 5.8 (C++)
//
// WHY A GAME STATE: it replicates to every client, and Narrative recommends it as the home of the party component (shared quests and
// dialogue). GM_BlackwoodHollow derives from AGameModeBase, so AGameStateBase is the right parent.
//
// MEMBERSHIP (server): AGameStateBase::AddPlayerState / RemovePlayerState are the engine's join / leave hooks. The host's player state is
// added first, so the host is the leader (index 0 of the party list). AddPlayerState runs while the PlayerState is still being
// initialised (PostInitializeComponents), BEFORE the controller's PlayerState pointer is set, which Narrative's AddPartyMember needs.
// Registration is therefore deferred: pending states are retried on a short timer until their controller points back at them.
//
// NARRATIVE COMPONENT PER PLAYER: Narrative's party members are UNarrativeComponents on the player controllers. If the controller already
// owns one (a Blueprint component) it is used; otherwise a UNarrativeComponent is created at runtime on the controller (replicated, no
// asset edit). The player's Narrative INVENTORY stays on the PlayerState, untouched.
//
// COMBAT FLAG: UBH_PartyStateSubsystem only exists on the server; bPartyInCombat mirrors its answer here so clients (death prompt) can read it.
//
// Hook-up: set GameStateClass = BH_GameState on GM_BlackwoodHollow (Blueprint default, see the phase 11P post-build steps).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/TimerHandle.h"
#include "BH_GameState.generated.h"

class UBH_PartyComponent;
class UNarrativeComponent;
class APlayerController;

/** Phase 12F. A world flag was set or cleared (any machine: the flag list replicates). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FBH_OnWorldFlagChanged, FName, Flag, bool, bIsSet);

UCLASS()
class BLACKWOODHOLLOWBETA_API ABH_GameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	ABH_GameState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void AddPlayerState(APlayerState* PlayerState) override;
	virtual void RemovePlayerState(APlayerState* PlayerState) override;

	/** The party component (any machine; its member list replicates). */
	UFUNCTION(BlueprintPure, Category = "BH|Party")
	UBH_PartyComponent* GetPartyComponent() const { return PartyComponent; }

	/** Any machine: the party is in combat (replicated copy of UBH_PartyStateSubsystem::IsPartyInCombat). */
	UFUNCTION(BlueprintPure, Category = "BH|Party")
	bool IsPartyInCombat() const { return bPartyInCombat; }

	/** SERVER. Written by UBH_PartyStateSubsystem whenever the party enters / leaves combat. */
	void SetPartyInCombat(bool bInCombat);

	/** The party leader's player state (the host; index 0). Null with an empty party. */
	UFUNCTION(BlueprintPure, Category = "BH|Party")
	APlayerState* GetPartyLeaderState() const;

	// -- World flags (Phase 12F) -----------------------------------------------------------------
	// A tiny server-owned set of named progress flags ("Island1.SpanGateOpen"). World progress lives on the game state, so it survives a party
	// wipe and reaches late joiners. Phase 14 quests set flags through SetWorldFlag; ABH_WorldGate listens to OnWorldFlagChanged.

	/** The Span Gate flag: "Island1.SpanGateOpen". */
	static FName SpanGateOpenFlag() { return FName(TEXT("Island1.SpanGateOpen")); }

	/** The game state of Context's world as an ABH_GameState, or null. */
	static ABH_GameState* Get(const UObject* WorldContext);

	/** SERVER. Sets (or, with bValue false, clears) a world flag. No-op on a client. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "BH|World")
	void SetWorldFlag(FName Flag, bool bValue = true);

	/** Any machine. True if the flag is currently set. */
	UFUNCTION(BlueprintPure, Category = "BH|World")
	bool HasWorldFlag(FName Flag) const { return WorldFlags.Contains(Flag); }

	/** Any machine, after the flag list changed (the server fires it from SetWorldFlag, clients from the RepNotify). */
	UPROPERTY(BlueprintAssignable, Category = "BH|World")
	FBH_OnWorldFlagChanged OnWorldFlagChanged;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BH|Party")
	TObjectPtr<UBH_PartyComponent> PartyComponent;

	/** The set flags. Replicated. */
	UPROPERTY(ReplicatedUsing = OnRep_WorldFlags)
	TArray<FName> WorldFlags;

	UFUNCTION()
	void OnRep_WorldFlags();

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "BH|Party")
	bool bPartyInCombat = false;

	/** Seconds between registration retries for player states whose controller is not wired up yet. */
	UPROPERTY(EditDefaultsOnly, Category = "BH|Party", meta = (ClampMin = "0.02", ForceUnits = "s"))
	float RegistrationRetryInterval = 0.1f;

	/** Gives up on a player state that never gets a controller after this many retries (log + drop). */
	UPROPERTY(EditDefaultsOnly, Category = "BH|Party", meta = (ClampMin = "1"))
	int32 MaxRegistrationRetries = 100;

private:
	/** Server: try to register every pending state; re-arms the retry timer while some remain. */
	void ProcessPendingRegistrations();

	/** Server: registers PlayerState now. @return true when done (registered or hopeless), false to retry later. */
	bool TryRegisterPlayerState(APlayerState* PlayerState);

	/** The controller's Narrative component: an existing one, or a new runtime one. */
	UNarrativeComponent* FindOrCreateNarrativeComponent(APlayerController* Controller) const;

	struct FPendingRegistration
	{
		TWeakObjectPtr<APlayerState> State;
		int32 Attempts = 0;
	};
	TArray<FPendingRegistration> PendingRegistrations;
	FTimerHandle RegistrationTimer;

	/** Client: the flag list the last broadcast was computed against (to turn a replicated array into per-flag events). */
	TArray<FName> LastBroadcastFlags;
};
