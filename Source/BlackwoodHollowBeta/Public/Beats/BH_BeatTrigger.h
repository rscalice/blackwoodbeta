// Blackwood Hollow - beat trigger volume (Phase 13B+)
// Target: Unreal Engine 5.8 (C++)
//
// A box volume that completes a beat when a player-controlled pawn walks in. SERVER only (the overlap is ignored on clients), then the
// replicated beat reaches everyone. Completing a beat that is ahead of the current one skips forward (see
// UBH_BeatTrackerComponent::CompleteBeat); completing an earlier one does nothing, so a volume can never move the beat backwards.
// Beats that already end on a world flag (barricade, Span Gate) do not need one; this is for beats with no flag (reaching the basin,
// the Port, the Gatehouse, the Span, taking the cache, touching the obelisk).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "BH_BeatTrigger.generated.h"

class UBoxComponent;

UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API ABH_BeatTrigger : public AActor
{
	GENERATED_BODY()

public:
	ABH_BeatTrigger();

	/** The beat this volume completes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Beat", meta = (Categories = "Beat"))
	FGameplayTag BeatToComplete;

	/** Ignore AI pawns wandering through. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Beat")
	bool bRequirePlayerControlled = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Beat")
	TObjectPtr<UBoxComponent> Box;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
};