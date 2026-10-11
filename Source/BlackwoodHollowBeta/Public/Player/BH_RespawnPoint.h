// Blackwood Hollow - hub respawn points (Phase 10B part 2: player death flow)
// Target: Unreal Engine 5.8 (C++)
//
// ABH_RespawnPoint is a placeable marker (Port Vanguard, the arena hub, ...). A dead player who picks "Respawn at <Hub>" is
// teleported to the NEAREST point (to where the body lies); UBH_RespawnPointSubsystem is the small per-world registry that
// finds it. Place the actor so its location is the floor point where the first player should stand, its yaw is the facing, and
// give it a HubName ("PortVanguard") - the death prompt shows "Respawn at Port Vanguard" (the name is split at capital letters).
//
// Phase 11G: the point at Port Vanguard is also the Heart-Fragment repair station. It carries a UBH_InteractableComponent (hold 1.5 s) that is
// offered ONLY to a Fractured player: with RepairShardCost (3) Corrupted Coral Shards the server removes the shards and the Fracture of
// THAT player alone; without them the prompt shows the cost and the reason "Not enough shards".
//
// Server only: the registry is not created on pure clients (only the server resolves the destination), the actor itself is not
// replicated (it is a level actor present on every machine, like any static marker).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/BH_InteractableComponent.h"
#include "Subsystems/WorldSubsystem.h"
#include "BH_RespawnPoint.generated.h"

class UArrowComponent;
class USceneComponent;

UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API ABH_RespawnPoint : public AActor, public IBH_InteractableOwner
{
	GENERATED_BODY()

public:
	ABH_RespawnPoint();

	/** Id of the hub this point belongs to ("PortVanguard"). Shown on the death prompt. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BH|Respawn")
	FName HubName = TEXT("PortVanguard");

	/** Radius (cm) of the ring extra players are spread on, so several respawns on the same point do not overlap. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BH|Respawn", meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float SpawnSpreadRadius = 120.f;

	/** Floor trace length above / below the actor when snapping the spawn to the ground. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BH|Respawn", meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float GroundTraceDistance = 400.f;

	/**
	 * World location a player capsule (half height CapsuleHalfHeight) should be teleported to. SlotIndex 0 = the point itself,
	 * 1..4 = four positions on the spread ring (further indices wrap around the ring). Snapped to the floor under the point.
	 */
	UFUNCTION(BlueprintCallable, Category = "BH|Respawn")
	FVector ComputeSpawnLocation(float CapsuleHalfHeight, int32 SlotIndex) const;

	UFUNCTION(BlueprintPure, Category = "BH|Respawn")
	FRotator GetSpawnRotation() const { return FRotator(0.f, GetActorRotation().Yaw, 0.f); }

	/** Corrupted Coral Shards the Fracture repair costs (balance.md: 3). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BH|Respawn|Repair", meta = (ClampMin = "1"))
	int32 RepairShardCost = 3;

	// -- IBH_InteractableOwner (Fracture repair) -------------------------------------------------
	virtual bool BH_CanInteract(const APawn* InteractingPawn, FText& OutDenyReason) const override;
	virtual FText BH_GetPromptAction(const APawn* InteractingPawn) const override;
	virtual void BH_OnInteractionCompleted(APawn* InteractingPawn) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BH|Respawn")
	TObjectPtr<USceneComponent> SceneRoot;

	/** Editor visual: the facing of the respawned player. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BH|Respawn")
	TObjectPtr<UArrowComponent> FacingArrow;

	/** The repair prompt (offered to Fractured players only, see BH_CanInteract). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BH|Respawn|Repair")
	TObjectPtr<UBH_InteractableComponent> RepairInteractable;
};

/** Per-world registry of the respawn points (server only). */
UCLASS()
class BLACKWOODHOLLOWBETA_API UBH_RespawnPointSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	static UBH_RespawnPointSubsystem* Get(const UObject* WorldContext);

	void RegisterPoint(ABH_RespawnPoint* Point);
	void UnregisterPoint(ABH_RespawnPoint* Point);

	/** The registered point nearest to Location (3D distance), or nullptr when none is registered. */
	ABH_RespawnPoint* FindNearest(const FVector& Location) const;

	int32 GetNumPoints() const { return Points.Num(); }

private:
	TArray<TWeakObjectPtr<ABH_RespawnPoint>> Points;
};
