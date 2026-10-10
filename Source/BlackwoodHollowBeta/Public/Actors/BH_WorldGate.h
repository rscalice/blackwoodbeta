// Blackwood Hollow - a gate that opens on a world flag (Phase 12F)
// Target: Unreal Engine 5.8 (C++)
//
// The Island 1 Span Gate. It does not know WHY it opens: it opens when the world flag WorldFlagName (default "Island1.SpanGateOpen") is set on
// ABH_GameState. Phase 14 sets that flag from the quest; until then bh.World.OpenSpanGate does.
//
// WORLD FLAG IS THE SOURCE OF TRUTH (#85). ABH_GameState is always relevant and its flag list replicates to every client, whereas this actor's
// own bOpen can be missed by a client that was out of range (or had its World Partition cell unloaded) when it changed. So EVERY machine reads
// the flag in BeginPlay and follows OnWorldFlagChanged (retrying until the game state exists, it can arrive after the level actors on a client).
// The SERVER additionally mirrors the flag into the replicated bOpen (kept for compatibility, and as the fallback when there is no BH game
// state). The actor is also bAlwaysRelevant (few and tiny), as belt and braces for the in-range case.
// On a change the actor is hidden, collision goes off, it stops blocking navigation, and BP_OnGateOpenChanged fires for a slide / dissolve effect.
// The world flag, and so the open gate, survives a party wipe.
//
// NAVIGATION: as with ABH_Breakable, the gap only enters the navmesh at runtime when the RecastNavMesh generates Dynamically.
//
// Setup in a Blueprint child: BodyMesh (graybox gate), WorldFlagName on the instance.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/TimerHandle.h"
#include "BH_WorldGate.generated.h"

class UStaticMeshComponent;

UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API ABH_WorldGate : public AActor
{
	GENERATED_BODY()

public:
	ABH_WorldGate();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** The world flag (on ABH_GameState) that opens this gate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|WorldGate")
	FName WorldFlagName = FName(TEXT("Island1.SpanGateOpen"));

	/** Hide the mesh when open. Turn off to keep an open-gate mesh in place (collision and nav blocking still go away). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|WorldGate")
	bool bHideWhenOpen = true;

	UFUNCTION(BlueprintPure, Category = "BH|WorldGate")
	bool IsOpen() const { return bOpen; }

	/** Both machines, when the open state changed (also for a client that returns after missing the change). For a slide / dissolve effect. */
	UFUNCTION(BlueprintImplementableEvent, Category = "BH|WorldGate")
	void BP_OnGateOpenChanged(bool bNewOpen);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

private:
	/** Delegate target for ABH_GameState::OnWorldFlagChanged (any machine). */
	UFUNCTION()
	void HandleWorldFlagChanged(FName Flag, bool bIsSet);

	UFUNCTION()
	void OnRep_Open();

	/** Any machine: binds to the game state, or re-arms a retry while it is not there yet. */
	void TryBindGameState();

	/** SERVER. Mirrors the world flag into the replicated bOpen. */
	void SetOpen(bool bNewOpen);

	/** The open state this machine should show: the world flag when a game state exists, otherwise the replicated bOpen. */
	bool ComputeEffectiveOpen() const;

	/** Recomputes the effective state and, when it changed, applies it (collision, nav, visibility) and fires BP_OnGateOpenChanged. */
	void RefreshOpenState();

	UPROPERTY(ReplicatedUsing = OnRep_Open)
	bool bOpen = false;

	FTimerHandle BindRetryTimer;
	bool bBoundToGameState = false;

	/** What this machine currently shows (local, not replicated). */
	bool bAppliedOpen = false;
	bool bHasApplied = false;
};
