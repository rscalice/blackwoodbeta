// Blackwood Hollow - harvest node (Phase 11C)
// Target: Unreal Engine 5.8 (C++)
//
// A plant / coral cluster the player harvests by HOLDING interact (HoldSeconds on the Interactable, default 1.5 s - the hold itself,
// its replicated progress ring and the cancel rules live in UBH_InteractorComponent). On completion the SERVER gives the harvester
// RandRange(YieldMin, YieldMax) of RewardItem (default Corrupted Coral Shard) straight into their inventory, marks the node depleted
// (replicated -> depleted visual on every machine) and starts the regrow timer (RegrowSeconds, default 600 = 10 min).
// First to finish wins the node; a second player holding at the same moment is cancelled by the server (the node is no longer usable).
//
// Regrow is real server time, not saved across a session restart (the level reloads fresh). Party wipe / respawn do not touch it.
// bh.Loot.ResetContainers regrows the node immediately.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/BH_InteractableComponent.h"
#include "Loot/BH_LootTypes.h"
#include "BH_HarvestNode.generated.h"

class UNarrativeItem;
class UStaticMesh;
class UStaticMeshComponent;

UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API ABH_HarvestNode : public AActor, public IBH_InteractableOwner
{
	GENERATED_BODY()

public:
	ABH_HarvestNode();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** What the node yields. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Harvest")
	TSoftClassPtr<UNarrativeItem> RewardItem = TSoftClassPtr<UNarrativeItem>(FSoftObjectPath(BH_LootPaths::CorruptedCoralShard));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Harvest", meta = (ClampMin = "1"))
	int32 YieldMin = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Harvest", meta = (ClampMin = "1"))
	int32 YieldMax = 2;

	/** Seconds until a depleted node is harvestable again. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Harvest", meta = (ClampMin = "1.0", ForceUnits = "s"))
	float RegrowSeconds = 600.f;

	/** Mesh shown while depleted. Empty = hide BodyMesh instead (the interactable and its greyed prompt stay). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Harvest")
	TObjectPtr<UStaticMesh> DepletedMesh;

	UFUNCTION(BlueprintPure, Category = "BH|Harvest")
	bool IsDepleted() const { return bDepleted; }

	/** Fires on every machine when the node is depleted / regrown. Particles, sound, scale-in go here. */
	UFUNCTION(BlueprintImplementableEvent, Category = "BH|Harvest")
	void BP_OnDepletedChanged(bool bNowDepleted);

	// -- IBH_InteractableOwner -----------------------------------------------------------------
	virtual bool BH_CanInteract(const APawn* InteractingPawn, FText& OutDenyReason) const override;
	virtual void BH_OnInteractionCompleted(APawn* InteractingPawn) override;
	virtual void BH_DebugReset() override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBH_InteractableComponent> Interactable;

private:
	UFUNCTION()
	void OnRep_Depleted();

	void ApplyDepletedVisual();
	void Regrow();

	UPROPERTY(ReplicatedUsing = OnRep_Depleted)
	bool bDepleted = false;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> LiveMesh;

	FTimerHandle RegrowTimer;
};
