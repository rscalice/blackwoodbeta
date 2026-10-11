// Blackwood Hollow - the Warden obelisk (Phase 12F)
// Target: Unreal Engine 5.8 (C++)
//
// The Island 1 skill-teaching shrine. Touching it teaches THAT player Overload Burst: the server calls ABH_PlayerState::GrantOverloadBurst, which
// equips the fragment into the first free slot that accepts it and sets the replicated bOverloadBurstLearned flag. Players begin Island 1 without
// fragment skills (ABH_IslandRules::bStartWithoutFragmentSkills), so this is where the first skill comes from.
//
// Per player: a player who already learned it is not offered the prompt. Each partner learns by touching it themselves. The flag lives on the
// PlayerState, so every machine can tell who still needs it: GetNumPlayersWithoutBurst() and the BP_OnPendingChanged event drive a marker
// ("your partner has not touched the obelisk yet") in a Blueprint child.
//
// Survives a party wipe (it is progression, not run state).
//
// Setup in a Blueprint child: BodyMesh (graybox pillar), the prompt texts on Interactable, a marker reacting to BP_OnPendingChanged.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/TimerHandle.h"
#include "Interaction/BH_InteractableComponent.h"
#include "BH_WardenObelisk.generated.h"

class UStaticMeshComponent;

UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API ABH_WardenObelisk : public AActor, public IBH_InteractableOwner
{
	GENERATED_BODY()

public:
	ABH_WardenObelisk();

	/** Seconds between checks of who has learned the skill (drives BP_OnPendingChanged). Not used on a dedicated server. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Obelisk", meta = (ClampMin = "0.1", ForceUnits = "s"))
	float PendingPollInterval = 0.5f;

	/** Any machine. How many connected players have not learned Overload Burst yet. */
	UFUNCTION(BlueprintPure, Category = "BH|Obelisk")
	int32 GetNumPlayersWithoutBurst() const;

	/** Any machine. True if the local player has learned it. */
	UFUNCTION(BlueprintPure, Category = "BH|Obelisk")
	bool HasLocalPlayerLearned() const;

	/** Any machine. True if the local player has learned it and at least one other player has not (the "mark your partner" case). */
	UFUNCTION(BlueprintPure, Category = "BH|Obelisk")
	bool IsPartnerPending() const;

	/** Fires on every non-dedicated machine when the number of players without the skill changes. For a marker / glow. */
	UFUNCTION(BlueprintImplementableEvent, Category = "BH|Obelisk")
	void BP_OnPendingChanged(int32 NumWithoutBurst);

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
	void PollPending();

	FTimerHandle PendingTimer;
	int32 LastPendingCount = -1;
};
