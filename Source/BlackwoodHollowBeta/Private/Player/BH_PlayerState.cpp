// Blackwood Hollow - player state holding the Narrative inventory (implementation)

#include "Player/BH_PlayerState.h"
#include "InventoryComponent.h"
#include "Components/ActorComponent.h"
#include "UObject/UnrealType.h"

ABH_PlayerState::ABH_PlayerState()
{
	Inventory = CreateDefaultSubobject<UNarrativeInventoryComponent>(TEXT("Inventory"));
	Inventory->SetIsReplicated(true);
	// Narrative Inventory replicates its item UObjects through the legacy virtual ReplicateSubobjects(). The project
	// enables net.SubObjects.DefaultUseSubObjectReplicationList=1 (GASP template), which would skip that path and
	// leave every item null on clients, so opt this component back into the legacy path.
	// (The flag is protected on UActorComponent with no setter, so it is written through reflection.)
	if (const FBoolProperty* Flag = CastField<FBoolProperty>(UActorComponent::StaticClass()->FindPropertyByName(TEXT("bReplicateUsingRegisteredSubObjectList"))))
	{
		Flag->SetPropertyValue_InContainer(Inventory, false);
	}

	// PlayerState defaults to ~1 Hz; equipment changes should reach the client quickly (we also ForceNetUpdate on changes).
	SetNetUpdateFrequency(10.f);
}
