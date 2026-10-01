// Blackwood Hollow - player state holding the Narrative inventory (implementation)

#include "Player/BH_PlayerState.h"
#include "InventoryComponent.h"

ABH_PlayerState::ABH_PlayerState()
{
	Inventory = CreateDefaultSubobject<UNarrativeInventoryComponent>(TEXT("Inventory"));
	Inventory->SetIsReplicated(true);

	// PlayerState defaults to ~1 Hz; equipment changes should reach the client quickly (we also ForceNetUpdate on changes).
	SetNetUpdateFrequency(10.f);
}
