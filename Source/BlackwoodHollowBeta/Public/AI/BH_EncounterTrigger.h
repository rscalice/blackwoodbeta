// Blackwood Hollow - Phase 12G encounter trigger
// Target: Unreal Engine 5.8 (C++)
//
// A box volume that starts its linked wave spawners (ABH_EnemyWaveSpawner) the first time a player pawn enters it. Server only logic
// (GetNetMode() != NM_Client); the actor is placed in the level and not replicated. One shot: it never fires again, so a cleared encounter stays
// cleared and a party wipe is handled by the spawners themselves (they restart their current wave). Used for the Choke ambush.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BH_EncounterTrigger.generated.h"

class UBoxComponent;
class ABH_EnemyWaveSpawner;

UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API ABH_EncounterTrigger : public AActor
{
	GENERATED_BODY()

public:
	ABH_EncounterTrigger();

	/** Spawners started when the first player enters. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Encounter")
	TArray<TObjectPtr<ABH_EnemyWaveSpawner>> Spawners;

	/** Half extents (cm) of the trigger box. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BH|Encounter", meta = (ClampMin = "10.0"))
	FVector BoxExtent = FVector(1300.0, 1300.0, 400.0);

	UFUNCTION(BlueprintPure, Category = "BH|Encounter")
	bool HasFired() const { return bFired; }

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;

private:
	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UBoxComponent> Box;

	bool bFired = false;
};
