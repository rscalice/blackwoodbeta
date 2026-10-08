// Blackwood Hollow - hub respawn points (Phase 10B part 2: player death flow)
// Target: Unreal Engine 5.8 (C++)
//
// ABH_RespawnPoint is a placeable marker (Port Vanguard, the arena hub, ...). A dead player who picks "Respawn at <Hub>" is
// teleported to the NEAREST point (to where the body lies); UBH_RespawnPointSubsystem is the small per-world registry that
// finds it. Place the actor so its location is the floor point where the first player should stand, its yaw is the facing, and
// give it a HubName ("PortVanguard") - the death prompt shows "Respawn at Port Vanguard" (the name is split at capital letters).
//
// Server only: the registry is not created on pure clients (only the server resolves the destination), the actor itself is not
// replicated (it is a level actor present on every machine, like any static marker).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "BH_RespawnPoint.generated.h"

class UArrowComponent;
class USceneComponent;

UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API ABH_RespawnPoint : public AActor
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

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BH|Respawn")
	TObjectPtr<USceneComponent> SceneRoot;

	/** Editor visual: the facing of the respawned player. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BH|Respawn")
	TObjectPtr<UArrowComponent> FacingArrow;
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
