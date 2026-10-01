// Blackwood Hollow - player state holding the Narrative inventory
// Target: Unreal Engine 5.8 (C++)
//
// Narrative Inventory expects a player's UNarrativeInventoryComponent on the PlayerState (it survives pawn
// respawns and replicates to the owning client). GM_BlackwoodHollow uses the Blueprint child
// /Game/Game/PS_BlackwoodHollow.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "BH_PlayerState.generated.h"

class UNarrativeInventoryComponent;

UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API ABH_PlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	ABH_PlayerState();

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Inventory")
	UNarrativeInventoryComponent* GetInventory() const { return Inventory; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BlackwoodHollow|Inventory")
	TObjectPtr<UNarrativeInventoryComponent> Inventory;
};
