// Blackwood Hollow - loose item pickup (implementation)

#include "Loot/BH_WorldPickup.h"
#include "Loot/BH_LootLibrary.h"
#include "NarrativeItem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"

ABH_WorldPickup::ABH_WorldPickup()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetNetUpdateFrequency(5.f);

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	SetRootComponent(BodyMesh);
	// A pickup is walked through, not bumped into; it still answers visibility traces so it can be seen / outlined.
	BodyMesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);

	Interactable = CreateDefaultSubobject<UBH_InteractableComponent>(TEXT("Interactable"));
	Interactable->PromptName = NSLOCTEXT("BlackwoodHollow", "PickupName", "Corrupted Coral Shard");
	Interactable->PromptAction = NSLOCTEXT("BlackwoodHollow", "PickupAction", "Pick up");
	Interactable->HoldSeconds = 0.f;
	Interactable->InteractRange = 180.f;
}

void ABH_WorldPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABH_WorldPickup, bCollected);
}

bool ABH_WorldPickup::BH_CanInteract(const APawn* InteractingPawn, FText& OutDenyReason) const
{
	OutDenyReason = FText::GetEmpty();
	return InteractingPawn && !bCollected;
}

void ABH_WorldPickup::BH_OnInteractionCompleted(APawn* InteractingPawn)
{
	if (!HasAuthority() || !InteractingPawn || bCollected)
	{
		return;
	}
	if (UClass* LoadedClass = ItemClass.LoadSynchronous())
	{
		// Inventory full: the pickup stays in the world for someone with room.
		if (UBH_LootLibrary::GrantItemToPawn(InteractingPawn, LoadedClass, FMath::Max(1, Quantity)) <= 0)
		{
			return;
		}
	}
	bCollected = true;
	OnRep_Collected(); // the server does not get the OnRep call
	ForceNetUpdate();
}

void ABH_WorldPickup::BH_DebugReset()
{
	if (HasAuthority() && bCollected)
	{
		bCollected = false;
		OnRep_Collected();
		ForceNetUpdate();
	}
}

void ABH_WorldPickup::OnRep_Collected()
{
	// Hidden actors are skipped by the interaction scan, which is what removes the prompt.
	SetActorHiddenInGame(bCollected);
	BP_OnCollectedChanged(bCollected);
}
