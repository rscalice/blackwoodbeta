// Blackwood Hollow - a gate that opens on a world flag (implementation)

#include "Actors/BH_WorldGate.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "Player/BH_GameState.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogBHWorldGate, Log, All);

ABH_WorldGate::ABH_WorldGate()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true; // a handful of tiny actors; also the world flag covers a client that misses a change (#85)
	SetNetUpdateFrequency(5.f);

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	SetRootComponent(BodyMesh);
	BodyMesh->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	BodyMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
}

void ABH_WorldGate::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABH_WorldGate, bOpen);
}

void ABH_WorldGate::BeginPlay()
{
	Super::BeginPlay();
	RefreshOpenState(); // the replicated bOpen as a first guess; the world flag takes over once the game state is found
	TryBindGameState();
}

void ABH_WorldGate::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(BindRetryTimer);
	if (bBoundToGameState)
	{
		if (ABH_GameState* BHGameState = ABH_GameState::Get(this))
		{
			BHGameState->OnWorldFlagChanged.RemoveDynamic(this, &ABH_WorldGate::HandleWorldFlagChanged);
		}
		bBoundToGameState = false;
	}
	Super::EndPlay(EndPlayReason);
}

void ABH_WorldGate::TryBindGameState()
{
	ABH_GameState* BHGameState = ABH_GameState::Get(this);
	if (!BHGameState)
	{
		// The game state can arrive after the level actors begin play (always on a client): retry shortly.
		GetWorldTimerManager().SetTimer(BindRetryTimer, this, &ABH_WorldGate::TryBindGameState, 0.25f, false);
		return;
	}
	if (!bBoundToGameState)
	{
		BHGameState->OnWorldFlagChanged.AddDynamic(this, &ABH_WorldGate::HandleWorldFlagChanged);
		bBoundToGameState = true;
	}
	SetOpen(BHGameState->HasWorldFlag(WorldFlagName)); // server only (no-op on a client)
	RefreshOpenState();
}

void ABH_WorldGate::HandleWorldFlagChanged(FName Flag, bool bIsSet)
{
	if (Flag == WorldFlagName)
	{
		SetOpen(bIsSet);
		RefreshOpenState();
	}
}

void ABH_WorldGate::SetOpen(bool bNewOpen)
{
	if (GetNetMode() == NM_Client || bOpen == bNewOpen)
	{
		return;
	}
	bOpen = bNewOpen;
	ForceNetUpdate();
	UE_LOG(LogBHWorldGate, Log, TEXT("%s is now %s (flag %s)."), *GetNameSafe(this), bOpen ? TEXT("open") : TEXT("closed"), *WorldFlagName.ToString());
}

void ABH_WorldGate::OnRep_Open()
{
	RefreshOpenState();
}

bool ABH_WorldGate::ComputeEffectiveOpen() const
{
	if (const ABH_GameState* BHGameState = ABH_GameState::Get(this))
	{
		return BHGameState->HasWorldFlag(WorldFlagName);
	}
	return bOpen;
}

void ABH_WorldGate::RefreshOpenState()
{
	const bool bNewOpen = ComputeEffectiveOpen();
	const bool bFirst = !bHasApplied;
	if (!bFirst && bNewOpen == bAppliedOpen)
	{
		return;
	}
	bHasApplied = true;
	bAppliedOpen = bNewOpen;
	if (bFirst && !bNewOpen)
	{
		return; // the level default is already the closed state
	}

	SetActorEnableCollision(!bNewOpen);
	if (BodyMesh)
	{
		// Out of (or back into) the nav build; the navmesh only updates at runtime when it generates Dynamically.
		BodyMesh->SetCanEverAffectNavigation(!bNewOpen);
	}
	SetActorHiddenInGame(bNewOpen && bHideWhenOpen);
	BP_OnGateOpenChanged(bNewOpen);
}
