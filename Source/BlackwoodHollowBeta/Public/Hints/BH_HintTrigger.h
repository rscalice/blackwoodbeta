// Blackwood Hollow - hint trigger volume (Phase 13E)
// Target: Unreal Engine 5.8 (C++)
//
// A box volume that fires a first-time hint for the player who walks into it (a trigger "at a beat"). SERVER only: the overlap is ignored on
// clients, the server tells just that player's machine (UBH_PlayerHintComponent), and the player's profile decides whether it shows.
// The volume references the hint asset directly, so the asset is cooked with the level. Each player is told once per session.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BH_HintTrigger.generated.h"

class UBoxComponent;
class UBH_HintData;

UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API ABH_HintTrigger : public AActor
{
	GENERATED_BODY()

public:
	ABH_HintTrigger();

	/** The hint this volume fires. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hint")
	TObjectPtr<UBH_HintData> Hint;

	/** Ignore AI pawns wandering through. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hint")
	bool bRequirePlayerControlled = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hint")
	TObjectPtr<UBoxComponent> Box;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
};
