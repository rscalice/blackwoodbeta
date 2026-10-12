// Blackwood Hollow - the Warden Watch beacon relight (Phase 13-0, #100)
// Target: Unreal Engine 5.8 (C++)
//
// A hold-to-relight interactable (about 2 s) at the Warden Watch on the south peak. When any player completes it, the SERVER sets the world flag
// WorldFlagName (default "Island1.SpanGateOpen", the same flag bh.World.OpenSpanGate sets and ABH_WorldGate listens to) on ABH_GameState.
//
// THE WORLD FLAG IS THE SOURCE OF TRUTH, exactly as for ABH_WorldGate (#85): every machine reads the flag in BeginPlay and follows
// OnWorldFlagChanged (retrying until the game state exists), so the lit state is the same for the whole party, survives a party wipe and reaches
// late joiners. The server mirrors the flag into the replicated bLit as the fallback when there is no BH game state. The actor is bAlwaysRelevant.
// Once lit, BH_CanInteract is false on every machine, so the prompt disappears for everyone.
//
// Blueprint child: BodyMesh (brazier), a teal light / emissive driven from BP_OnLitChanged.
// PHASE 14 TODO: BP_OnRelit fires ONCE on the server at the moment a player relights it (not for late joiners). Start "Guardians of the Span" there.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/TimerHandle.h"
#include "Interaction/BH_InteractableComponent.h"
#include "BH_BeaconRelight.generated.h"

class UStaticMeshComponent;

UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API ABH_BeaconRelight : public AActor, public IBH_InteractableOwner
{
	GENERATED_BODY()

public:
	ABH_BeaconRelight();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** The world flag (on ABH_GameState) this beacon sets and follows. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Beacon")
	FName WorldFlagName = FName(TEXT("Island1.SpanGateOpen"));

	/** Any machine. True once the beacon is lit (the world flag). */
	UFUNCTION(BlueprintPure, Category = "BH|Beacon")
	bool IsLit() const { return bAppliedLit; }

	/** Every machine, when the lit state changed (also for a client that returns after missing the change, and once at begin play when already lit). Drive the light / emissive here. */
	UFUNCTION(BlueprintImplementableEvent, Category = "BH|Beacon")
	void BP_OnLitChanged(bool bNewLit);

	/** SERVER only, once, when a player relights the beacon. TODO Phase 14: start "Guardians of the Span" here. */
	UFUNCTION(BlueprintImplementableEvent, Category = "BH|Beacon")
	void BP_OnRelit(APawn* RelightedBy);

	// -- IBH_InteractableOwner -----------------------------------------------------------------
	virtual bool BH_CanInteract(const APawn* InteractingPawn, FText& OutDenyReason) const override;
	virtual void BH_OnInteractionCompleted(APawn* InteractingPawn) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBH_InteractableComponent> Interactable;

private:
	UFUNCTION()
	void HandleWorldFlagChanged(FName Flag, bool bIsSet);

	UFUNCTION()
	void OnRep_Lit();

	void TryBindGameState();
	bool ComputeEffectiveLit() const;
	void RefreshLitState();

	UPROPERTY(ReplicatedUsing = OnRep_Lit)
	bool bLit = false;

	FTimerHandle BindRetryTimer;
	bool bBoundToGameState = false;
	bool bAppliedLit = false;
	bool bHasApplied = false;
};
