// Blackwood Hollow - loose item pickup (Phase 11C)
// Target: Unreal Engine 5.8 (C++)
//
// A single item lying in the world (the loose Corrupted Coral Shard). Instant interaction: the SERVER grants Quantity of ItemClass to the
// player who pressed first, then marks the pickup collected (replicated -> hidden, no collision, not offered any more on every machine).
// First come, first served - this is one-of-a-kind world loot, unlike containers (one roll per player). The actor is hidden, not destroyed,
// so bh.Loot.ResetContainers can bring it back.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/BH_InteractableComponent.h"
#include "Loot/BH_LootTypes.h"
#include "BH_WorldPickup.generated.h"

class UNarrativeItem;
class UStaticMesh;
class UStaticMeshComponent;

UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API ABH_WorldPickup : public AActor, public IBH_InteractableOwner
{
	GENERATED_BODY()

public:
	ABH_WorldPickup();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Pickup")
	TSoftClassPtr<UNarrativeItem> ItemClass = TSoftClassPtr<UNarrativeItem>(FSoftObjectPath(BH_LootPaths::CorruptedCoralShard));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Pickup", meta = (ClampMin = "1"))
	int32 Quantity = 1;

	UFUNCTION(BlueprintPure, Category = "BH|Pickup")
	bool IsCollected() const { return bCollected; }

	/**
	 * 13G-0. SERVER only. Spawns a pickup holding Quantity of ItemClass at Location: an item dropped from the Player Menu. The mesh is the item's
	 * Narrative PickupMesh (a small sphere when it has none), the prompt shows the item's name, and the actor is destroyed shortly after it is
	 * collected. @return null on a client or when the spawn fails.
	 */
	static ABH_WorldPickup* SpawnDroppedItem(UWorld* WorldContext, TSubclassOf<UNarrativeItem> DroppedClass, int32 DroppedQuantity, const FVector& Location);

	/** Fires on every machine when the pickup is collected / restored. Pickup sparkle, sound go here. */
	UFUNCTION(BlueprintImplementableEvent, Category = "BH|Pickup")
	void BP_OnCollectedChanged(bool bNowCollected);

	// -- IBH_InteractableOwner -----------------------------------------------------------------
	virtual bool BH_CanInteract(const APawn* InteractingPawn, FText& OutDenyReason) const override;
	virtual void BH_OnInteractionCompleted(APawn* InteractingPawn) override;
	virtual void BH_DebugReset() override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBH_InteractableComponent> Interactable;

private:
	UFUNCTION()
	void OnRep_Collected();

	UPROPERTY(ReplicatedUsing = OnRep_Collected)
	bool bCollected = false;

	/** 13C (#14). Server only. Quantity left after a partial pickup (inventory full); 0 = untouched. */
	int32 RemainingQuantity = 0;

	/** 13G-0. Server only. True for items dropped from the Player Menu: the actor is destroyed 2 s after it is collected. */
	bool bDestroyWhenCollected = false;

	/** 13G-0. Replicated look of a dropped item (the static mesh and prompt of a level-placed pickup are set in its Blueprint instead). */
	UPROPERTY(ReplicatedUsing = OnRep_DropVisual)
	TObjectPtr<UStaticMesh> DropMesh;

	UPROPERTY(ReplicatedUsing = OnRep_DropVisual)
	FText DropName;

	UFUNCTION()
	void OnRep_DropVisual();

	void ApplyDropVisual();
};
