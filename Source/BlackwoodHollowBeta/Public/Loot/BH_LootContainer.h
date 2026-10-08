// Blackwood Hollow - lootable container (Phase 11C)
// Target: Unreal Engine 5.8 (C++)
//
// A crate / chest / sack the party can open. Each player gets THEIR OWN roll, once: the server remembers who looted it
// (LootedKeys, one key per player from UBH_LootLibrary::GetPlayerKey) and rolls the configured Narrative loot tables straight into that
// player's PlayerState inventory. Nothing drops into the world, so there is nothing to race for or lose.
//
//   * bOpened (replicated) is the shared visual state: the first successful open swaps to OpenedMesh and fires BP_OnOpenedChanged.
//   * LootedKeys (replicated) drives the prompt: a player who already looted it is not offered the interaction any more.
//   * Party wipe / respawn / checkpoint (11F): NOTHING here is reset. The container is world state, owned by this actor, so it keeps
//     "opened by X" through a wipe. Only bh.Loot.ResetContainers (debug) clears it.
//
// Setup in a Blueprint child (BP_LootContainer_*): set the mesh on BodyMesh, OpenedMesh (optional), LootRolls (TableToRoll = DT_Loot_*,
// NumRolls, Chance) and the prompt name on the Interactable component.
// A roll picks one UNIFORM random row of the table, then applies that row's own Chance (Narrative behaviour) - so a "guaranteed 1-2 Incense"
// table has two rows (x1, x2) with Chance 1.0 and the roll NumRolls=1, Chance=1.0.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/BH_InteractableComponent.h"
#include "InventoryComponent.h"
#include "BH_LootContainer.generated.h"

class UStaticMesh;
class UStaticMeshComponent;

UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API ABH_LootContainer : public AActor, public IBH_InteractableOwner
{
	GENERATED_BODY()

public:
	ABH_LootContainer();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// -- Configuration ---------------------------------------------------------------------------

	/** What each player rolls on opening. Every entry is rolled in order for that player. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Loot")
	TArray<FLootTableRoll> LootRolls;

	/** Mesh shown once the container has been opened (optional; empty = keep the closed mesh and rely on BP_OnOpenedChanged). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Loot")
	TObjectPtr<UStaticMesh> OpenedMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Loot")
	FText OpenedPromptAction = NSLOCTEXT("BlackwoodHollow", "LootContainerOpen", "Open");

	// -- State -----------------------------------------------------------------------------------

	/** True once any player has opened it. Replicated. */
	UFUNCTION(BlueprintPure, Category = "BH|Loot")
	bool IsOpened() const { return bOpened; }

	/** True if this player already took their share. Both machines. */
	UFUNCTION(BlueprintPure, Category = "BH|Loot")
	bool HasPlayerLooted(const APlayerState* PlayerState) const;

	/** Fires on every machine when bOpened flips (open / debug reset). Play the lid animation, VFX, sound here. */
	UFUNCTION(BlueprintImplementableEvent, Category = "BH|Loot")
	void BP_OnOpenedChanged(bool bNowOpened);

	// -- IBH_InteractableOwner -----------------------------------------------------------------
	virtual bool BH_CanInteract(const APawn* InteractingPawn, FText& OutDenyReason) const override;
	virtual FText BH_GetPromptAction(const APawn* InteractingPawn) const override { return OpenedPromptAction; }
	virtual void BH_OnInteractionCompleted(APawn* InteractingPawn) override;
	virtual void BH_DebugReset() override;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBH_InteractableComponent> Interactable;

private:
	UFUNCTION()
	void OnRep_Opened();

	void ApplyOpenedVisual();

	UPROPERTY(ReplicatedUsing = OnRep_Opened)
	bool bOpened = false;

	/** Players that already looted this container (UBH_LootLibrary::GetPlayerKey). Replicated so clients can hide the prompt. */
	UPROPERTY(Replicated)
	TArray<FString> LootedKeys;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> ClosedMesh;
};
