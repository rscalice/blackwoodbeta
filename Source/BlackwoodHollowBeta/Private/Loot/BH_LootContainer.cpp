// Blackwood Hollow - lootable container (implementation)

#include "Loot/BH_LootContainer.h"
#include "Interaction/BH_InteractorComponent.h"
#include "Loot/BH_LootLibrary.h"
#include "Player/BH_PlayerState.h"
#include "NarrativeItem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogBHLootContainer, Log, All);

ABH_LootContainer::ABH_LootContainer()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetNetUpdateFrequency(5.f);

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	SetRootComponent(BodyMesh);
	BodyMesh->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	BodyMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore); // the camera must not be shoved by a crate

	Interactable = CreateDefaultSubobject<UBH_InteractableComponent>(TEXT("Interactable"));
	Interactable->PromptName = NSLOCTEXT("BlackwoodHollow", "LootContainerName", "Supply Crate");
	Interactable->PromptAction = NSLOCTEXT("BlackwoodHollow", "LootContainerAction", "Open");
	Interactable->HoldSeconds = 0.f;
}

void ABH_LootContainer::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABH_LootContainer, bOpened);
	DOREPLIFETIME(ABH_LootContainer, LootedKeys);
}

void ABH_LootContainer::BeginPlay()
{
	Super::BeginPlay();
	if (BodyMesh)
	{
		ClosedMesh = BodyMesh->GetStaticMesh();
	}
	ApplyOpenedVisual();
}

bool ABH_LootContainer::HasPlayerLooted(const APlayerState* PlayerState) const
{
	return PlayerState && LootedKeys.Contains(UBH_LootLibrary::GetPlayerKey(PlayerState));
}

bool ABH_LootContainer::BH_CanInteract(const APawn* InteractingPawn, FText& OutDenyReason) const
{
	OutDenyReason = FText::GetEmpty();
	// Already looted by this player: not offered at all (empty reason hides the prompt).
	return InteractingPawn && InteractingPawn->GetPlayerState() && !HasPlayerLooted(InteractingPawn->GetPlayerState());
}

void ABH_LootContainer::BH_OnInteractionCompleted(APawn* InteractingPawn)
{
	if (!HasAuthority() || !InteractingPawn)
	{
		return;
	}
	APlayerState* PlayerState = InteractingPawn->GetPlayerState();
	const ABH_PlayerState* BHPlayerState = Cast<ABH_PlayerState>(PlayerState);
	UNarrativeInventoryComponent* Inventory = BHPlayerState ? BHPlayerState->GetInventory() : nullptr;
	if (!PlayerState || HasPlayerLooted(PlayerState))
	{
		return;
	}

	// The player's share is spent now, even if the inventory turns out to be (partly) full: no re-rolling by emptying the bag.
	LootedKeys.Add(UBH_LootLibrary::GetPlayerKey(PlayerState));

	if (Inventory)
	{
		UBH_InteractorComponent* Interactor = UBH_InteractorComponent::Find(InteractingPawn);
		for (const FLootTableRoll& Roll : LootRolls)
		{
			if (!Roll.TableToRoll)
			{
				continue;
			}
			TArray<FItemAddResult> Results;
			Inventory->TryAddFromLootTable(Roll, Results);
			for (const FItemAddResult& Result : Results)
			{
				if (Interactor && Result.AmountGiven > 0 && Result.ItemClass)
				{
					Interactor->NotifyItemGranted(UBH_LootLibrary::GetItemDisplayName(Result.ItemClass), Result.AmountGiven);
				}
			}
		}
	}
	else
	{
		UE_LOG(LogBHLootContainer, Warning, TEXT("%s: %s has no Narrative inventory, nothing granted."), *GetNameSafe(this), *GetNameSafe(PlayerState));
	}

	if (!bOpened)
	{
		bOpened = true;
		OnRep_Opened(); // the server does not get the OnRep call
	}
	ForceNetUpdate();
}

void ABH_LootContainer::BH_DebugReset()
{
	if (!HasAuthority())
	{
		return;
	}
	LootedKeys.Reset();
	if (bOpened)
	{
		bOpened = false;
		OnRep_Opened();
	}
	ForceNetUpdate();
}

void ABH_LootContainer::OnRep_Opened()
{
	ApplyOpenedVisual();
	BP_OnOpenedChanged(bOpened);
}

void ABH_LootContainer::ApplyOpenedVisual()
{
	if (BodyMesh && OpenedMesh && ClosedMesh)
	{
		BodyMesh->SetStaticMesh(bOpened ? OpenedMesh.Get() : ClosedMesh.Get());
	}
}
