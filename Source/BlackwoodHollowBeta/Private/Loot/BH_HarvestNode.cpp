// Blackwood Hollow - harvest node (implementation)

#include "Loot/BH_HarvestNode.h"
#include "Loot/BH_LootLibrary.h"
#include "NarrativeItem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

ABH_HarvestNode::ABH_HarvestNode()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetNetUpdateFrequency(5.f);

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	SetRootComponent(BodyMesh);
	BodyMesh->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	BodyMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

	Interactable = CreateDefaultSubobject<UBH_InteractableComponent>(TEXT("Interactable"));
	Interactable->PromptName = NSLOCTEXT("BlackwoodHollow", "HarvestNodeName", "Corrupted Coral");
	Interactable->PromptAction = NSLOCTEXT("BlackwoodHollow", "HarvestNodeAction", "Harvest");
	Interactable->HoldSeconds = 1.5f;
}

void ABH_HarvestNode::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABH_HarvestNode, bDepleted);
}

void ABH_HarvestNode::BeginPlay()
{
	Super::BeginPlay();
	if (BodyMesh)
	{
		LiveMesh = BodyMesh->GetStaticMesh();
	}
	ApplyDepletedVisual();
}

void ABH_HarvestNode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* NodeWorld = GetWorld())
	{
		NodeWorld->GetTimerManager().ClearTimer(RegrowTimer);
	}
	Super::EndPlay(EndPlayReason);
}

bool ABH_HarvestNode::BH_CanInteract(const APawn* InteractingPawn, FText& OutDenyReason) const
{
	OutDenyReason = FText::GetEmpty();
	if (!InteractingPawn)
	{
		return false;
	}
	if (bDepleted)
	{
		OutDenyReason = NSLOCTEXT("BlackwoodHollow", "HarvestNodeDepleted", "Depleted");
		return false;
	}
	return true;
}

void ABH_HarvestNode::BH_OnInteractionCompleted(APawn* InteractingPawn)
{
	if (!HasAuthority() || !InteractingPawn || bDepleted)
	{
		return;
	}

	const int32 MinYield = FMath::Max(1, YieldMin);
	const int32 Yield = FMath::RandRange(MinYield, FMath::Max(MinYield, YieldMax));
	if (UClass* RewardClass = RewardItem.LoadSynchronous())
	{
		UBH_LootLibrary::GrantItemToPawn(InteractingPawn, RewardClass, Yield);
	}

	bDepleted = true;
	OnRep_Depleted(); // the server does not get the OnRep call
	ForceNetUpdate();

	if (UWorld* NodeWorld = GetWorld())
	{
		NodeWorld->GetTimerManager().SetTimer(RegrowTimer, this, &ABH_HarvestNode::Regrow, FMath::Max(1.f, RegrowSeconds), false);
	}
}

void ABH_HarvestNode::Regrow()
{
	if (!HasAuthority() || !bDepleted)
	{
		return;
	}
	bDepleted = false;
	OnRep_Depleted();
	ForceNetUpdate();
}

void ABH_HarvestNode::BH_DebugReset()
{
	if (!HasAuthority())
	{
		return;
	}
	if (UWorld* NodeWorld = GetWorld())
	{
		NodeWorld->GetTimerManager().ClearTimer(RegrowTimer);
	}
	Regrow();
}

void ABH_HarvestNode::OnRep_Depleted()
{
	ApplyDepletedVisual();
	BP_OnDepletedChanged(bDepleted);
}

void ABH_HarvestNode::ApplyDepletedVisual()
{
	if (!BodyMesh)
	{
		return;
	}
	if (DepletedMesh && LiveMesh)
	{
		BodyMesh->SetStaticMesh(bDepleted ? DepletedMesh.Get() : LiveMesh.Get());
		BodyMesh->SetVisibility(true);
	}
	else
	{
		BodyMesh->SetVisibility(!bDepleted);
	}
}
