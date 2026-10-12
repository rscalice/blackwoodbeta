// Blackwood Hollow - the Warden Watch beacon relight (implementation)

#include "Actors/BH_BeaconRelight.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "Player/BH_GameState.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogBHBeaconRelight, Log, All);

ABH_BeaconRelight::ABH_BeaconRelight()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(5.f);

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	SetRootComponent(BodyMesh);
	BodyMesh->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	BodyMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

	Interactable = CreateDefaultSubobject<UBH_InteractableComponent>(TEXT("Interactable"));
	Interactable->PromptName = NSLOCTEXT("BlackwoodHollow", "BeaconRelightName", "Warden Beacon");
	Interactable->PromptAction = NSLOCTEXT("BlackwoodHollow", "BeaconRelightAction", "Relight");
	Interactable->HoldSeconds = 2.f;
	Interactable->InteractRange = 300.f;
}

void ABH_BeaconRelight::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABH_BeaconRelight, bLit);
}

void ABH_BeaconRelight::BeginPlay()
{
	Super::BeginPlay();
	RefreshLitState();
	TryBindGameState();
}

void ABH_BeaconRelight::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(BindRetryTimer);
	if (bBoundToGameState)
	{
		if (ABH_GameState* BHGameState = ABH_GameState::Get(this))
		{
			BHGameState->OnWorldFlagChanged.RemoveDynamic(this, &ABH_BeaconRelight::HandleWorldFlagChanged);
		}
		bBoundToGameState = false;
	}
	Super::EndPlay(EndPlayReason);
}

void ABH_BeaconRelight::TryBindGameState()
{
	ABH_GameState* BHGameState = ABH_GameState::Get(this);
	if (!BHGameState)
	{
		GetWorldTimerManager().SetTimer(BindRetryTimer, this, &ABH_BeaconRelight::TryBindGameState, 0.25f, false);
		return;
	}
	if (!bBoundToGameState)
	{
		BHGameState->OnWorldFlagChanged.AddDynamic(this, &ABH_BeaconRelight::HandleWorldFlagChanged);
		bBoundToGameState = true;
	}
	if (GetNetMode() != NM_Client)
	{
		bLit = BHGameState->HasWorldFlag(WorldFlagName);
	}
	RefreshLitState();
}

void ABH_BeaconRelight::HandleWorldFlagChanged(FName Flag, bool bIsSet)
{
	if (Flag == WorldFlagName)
	{
		if (GetNetMode() != NM_Client && bLit != bIsSet)
		{
			bLit = bIsSet;
			ForceNetUpdate();
		}
		RefreshLitState();
	}
}

void ABH_BeaconRelight::OnRep_Lit()
{
	RefreshLitState();
}

bool ABH_BeaconRelight::ComputeEffectiveLit() const
{
	if (const ABH_GameState* BHGameState = ABH_GameState::Get(this))
	{
		return BHGameState->HasWorldFlag(WorldFlagName);
	}
	return bLit;
}

void ABH_BeaconRelight::RefreshLitState()
{
	const bool bNewLit = ComputeEffectiveLit();
	const bool bFirst = !bHasApplied;
	if (!bFirst && bNewLit == bAppliedLit)
	{
		return;
	}
	bHasApplied = true;
	bAppliedLit = bNewLit;
	if (bFirst && !bNewLit)
	{
		return; // the Blueprint default is the unlit state
	}
	BP_OnLitChanged(bNewLit);
}

bool ABH_BeaconRelight::BH_CanInteract(const APawn* InteractingPawn, FText& OutDenyReason) const
{
	OutDenyReason = FText::GetEmpty();
	return !ComputeEffectiveLit(); // lit: no prompt for anybody
}

void ABH_BeaconRelight::BH_OnInteractionCompleted(APawn* InteractingPawn)
{
	if (GetNetMode() == NM_Client || ComputeEffectiveLit())
	{
		return;
	}
	if (ABH_GameState* BHGameState = ABH_GameState::Get(this))
	{
		BHGameState->SetWorldFlag(WorldFlagName, true);
	}
	else
	{
		bLit = true;
		ForceNetUpdate();
		RefreshLitState();
	}
	UE_LOG(LogBHBeaconRelight, Log, TEXT("%s relit by %s (flag %s)."), *GetNameSafe(this), *GetNameSafe(InteractingPawn), *WorldFlagName.ToString());
	BP_OnRelit(InteractingPawn);
}
