// Blackwood Hollow - loose item pickup (implementation)

#include "Loot/BH_WorldPickup.h"
#include "Loot/BH_LootLibrary.h"
#include "NarrativeItem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
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
	DOREPLIFETIME_CONDITION(ABH_WorldPickup, DropMesh, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(ABH_WorldPickup, DropName, COND_InitialOnly);
}

ABH_WorldPickup* ABH_WorldPickup::SpawnDroppedItem(UWorld* WorldContext, TSubclassOf<UNarrativeItem> DroppedClass, int32 DroppedQuantity, const FVector& Location)
{
	if (!WorldContext || WorldContext->GetNetMode() == NM_Client || !DroppedClass || DroppedQuantity <= 0)
	{
		return nullptr;
	}
	const FTransform SpawnTransform(FRotator::ZeroRotator, Location);
	ABH_WorldPickup* Pickup = WorldContext->SpawnActorDeferred<ABH_WorldPickup>(ABH_WorldPickup::StaticClass(), SpawnTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Pickup)
	{
		return nullptr;
	}
	Pickup->ItemClass = TSoftClassPtr<UNarrativeItem>(FSoftObjectPath(DroppedClass.Get()));
	Pickup->Quantity = DroppedQuantity;
	Pickup->bDestroyWhenCollected = true;
	if (const UNarrativeItem* ItemDefaults = DroppedClass->GetDefaultObject<UNarrativeItem>())
	{
		Pickup->DropName = ItemDefaults->DisplayName;
		Pickup->DropMesh = ItemDefaults->PickupMesh.IsNull() ? nullptr : ItemDefaults->PickupMesh.LoadSynchronous();
	}
	if (!Pickup->DropMesh)
	{
		Pickup->DropMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	}
	Pickup->FinishSpawning(SpawnTransform);
	Pickup->ApplyDropVisual(); // the server does not get the OnRep call
	return Pickup;
}

void ABH_WorldPickup::OnRep_DropVisual()
{
	ApplyDropVisual();
}

void ABH_WorldPickup::ApplyDropVisual()
{
	if (DropMesh && BodyMesh)
	{
		BodyMesh->SetStaticMesh(DropMesh);
		// The engine sphere is 100 cm across: shrink it to a loot-sized orb. Item pickup meshes keep their own scale.
		if (DropMesh->GetPathName().StartsWith(TEXT("/Engine/BasicShapes/")))
		{
			BodyMesh->SetWorldScale3D(FVector(0.25f));
		}
	}
	if (!DropName.IsEmpty() && Interactable)
	{
		Interactable->PromptName = DropName;
	}
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
		// Inventory full: the rest stays in the world for someone with room (GrantItem shows "Inventory full" to the player).
		const int32 Wanted = RemainingQuantity > 0 ? RemainingQuantity : FMath::Max(1, Quantity);
		const int32 Given = UBH_LootLibrary::GrantItemToPawn(InteractingPawn, LoadedClass, Wanted);
		if (Given < Wanted)
		{
			RemainingQuantity = Wanted - Given;
			return;
		}
	}
	RemainingQuantity = 0;
	bCollected = true;
	OnRep_Collected(); // the server does not get the OnRep call
	ForceNetUpdate();
	if (bDestroyWhenCollected)
	{
		SetLifeSpan(2.f); // long enough for the collected state to reach every client
	}
}

void ABH_WorldPickup::BH_DebugReset()
{
	RemainingQuantity = 0;
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
