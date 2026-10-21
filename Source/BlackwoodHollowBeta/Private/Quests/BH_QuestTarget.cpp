
// Blackwood Hollow - quest target marker actor (Phase 14A-1, implementation)

#include "Quests/BH_QuestTarget.h"
#include "Components/SceneComponent.h"

ABH_QuestTarget::ABH_QuestTarget()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetCanBeDamaged(false);

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

#if WITH_EDITORONLY_DATA
	// Always loaded, so the compass can resolve a far target on every machine.
	bIsSpatiallyLoaded = false;
#endif
}
