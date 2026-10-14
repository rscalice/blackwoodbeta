// Blackwood Hollow - beat target marker actor (Phase 13B+, implementation)

#include "Beats/BH_BeatTarget.h"
#include "Components/SceneComponent.h"

ABH_BeatTarget::ABH_BeatTarget()
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