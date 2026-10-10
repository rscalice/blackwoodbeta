// Blackwood Hollow - a gate that opens on a world flag (Phase 12F)
// Target: Unreal Engine 5.8 (C++)
//
// The Island 1 Span Gate. It does not know WHY it opens: it opens when the world flag WorldFlagName (default "Island1.SpanGateOpen") is set on
// ABH_GameState. Phase 14 sets that flag from the quest; until then bh.World.OpenSpanGate does.
//
// The SERVER listens to ABH_GameState::OnWorldFlagChanged, sets the replicated bOpen, and every machine reacts in OnRep_Open (the server calls it
// directly): the actor is hidden, collision goes off, it stops blocking navigation, and BP_OnGateOpenChanged fires for a slide / dissolve effect.
// A late joiner receives bOpen with the actor, so it opens for them too. The world flag, and so the open gate, survives a party wipe.
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

	/** Both machines, after bOpen changed (also for a late joiner). For a slide / dissolve effect. */
	UFUNCTION(BlueprintImplementableEvent, Category = "BH|WorldGate")
	void BP_OnGateOpenChanged(bool bNewOpen);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

private:
	/** Delegate target for ABH_GameState::OnWorldFlagChanged (server). */
	UFUNCTION()
	void HandleWorldFlagChanged(FName Flag, bool bIsSet);

	UFUNCTION()
	void OnRep_Open();

	/** Server: binds to the game state, or re-arms a retry while it is not there yet. */
	void TryBindGameState();

	/** SERVER. */
	void SetOpen(bool bNewOpen);

	/** Applies bOpen on this machine (collision, nav, visibility). */
	void ApplyOpenState();

	UPROPERTY(ReplicatedUsing = OnRep_Open)
	bool bOpen = false;

	FTimerHandle BindRetryTimer;
	bool bBoundToGameState = false;
};
