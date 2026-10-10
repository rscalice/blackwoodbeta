// Blackwood Hollow - a breakable world blocker (implementation)

#include "Actors/BH_Breakable.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "Player/BH_GameState.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogBHBreakable, Log, All);

ABH_Breakable::ABH_Breakable()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true; // a handful of tiny actors; also the world flag covers a client that misses a change (#85)
	SetNetUpdateFrequency(5.f);

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	SetRootComponent(BodyMesh);
	BodyMesh->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	// The melee hitbox sweeps for Pawn / PhysicsBody objects, so the barricade is a PhysicsBody object (it stays static: no simulation).
	BodyMesh->SetCollisionObjectType(ECC_PhysicsBody);
	BodyMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

	Health = MaxHealth;
}

void ABH_Breakable::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABH_Breakable, Health);
	DOREPLIFETIME(ABH_Breakable, bBroken);
}

FName ABH_Breakable::GetResolvedBrokenFlag() const
{
	if (!BrokenWorldFlag.IsNone())
	{
		return BrokenWorldFlag;
	}
	return FName(*FString::Printf(TEXT("Breakable.%s"), *GetFName().ToString()));
}

void ABH_Breakable::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() != NM_Client)
	{
		Health = MaxHealth;
	}
	RefreshBrokenState(); // the replicated bBroken as a first guess; the world flag takes over once the game state is found
	TryBindGameState();
}

void ABH_Breakable::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(BindRetryTimer);
	if (bBoundToGameState)
	{
		if (ABH_GameState* BHGameState = ABH_GameState::Get(this))
		{
			BHGameState->OnWorldFlagChanged.RemoveDynamic(this, &ABH_Breakable::HandleWorldFlagChanged);
		}
		bBoundToGameState = false;
	}
	Super::EndPlay(EndPlayReason);
}

void ABH_Breakable::TryBindGameState()
{
	ABH_GameState* BHGameState = ABH_GameState::Get(this);
	if (!BHGameState)
	{
		// The game state can arrive after the level actors begin play (always on a client): retry shortly.
		GetWorldTimerManager().SetTimer(BindRetryTimer, this, &ABH_Breakable::TryBindGameState, 0.25f, false);
		return;
	}
	if (!bBoundToGameState)
	{
		BHGameState->OnWorldFlagChanged.AddDynamic(this, &ABH_Breakable::HandleWorldFlagChanged);
		bBoundToGameState = true;
		UE_LOG(LogBHBreakable, Log, TEXT("%s watches world flag %s (%s)."), *GetNameSafe(this), *GetResolvedBrokenFlag().ToString(), GetNetMode() == NM_Client ? TEXT("client") : TEXT("server"));
	}
	if (GetNetMode() != NM_Client && !bBroken && BHGameState->HasWorldFlag(GetResolvedBrokenFlag()))
	{
		BreakNow(); // the flag was set before this actor began play (a reloaded cell on the server)
		return;
	}
	RefreshBrokenState();
}

void ABH_Breakable::HandleWorldFlagChanged(FName Flag, bool bIsSet)
{
	if (bIsSet && Flag == GetResolvedBrokenFlag())
	{
		RefreshBrokenState();
	}
}

bool ABH_Breakable::ComputeEffectiveBroken() const
{
	if (bBroken)
	{
		return true;
	}
	const ABH_GameState* BHGameState = ABH_GameState::Get(this);
	return BHGameState && BHGameState->HasWorldFlag(GetResolvedBrokenFlag());
}

void ABH_Breakable::BH_ReceiveMeleeHit(AActor* Attacker, float DamageMultiplier, const FHitResult& Hit)
{
	// Server only: ANS_MeleeHitbox already gates on authority, this keeps a stray call from a client harmless.
	if (GetNetMode() == NM_Client || bBroken || DamageMultiplier < MinHitMultiplier)
	{
		return;
	}

	Health = FMath::Max(0.f, Health - DamagePerHit * FMath::Max(0.f, DamageMultiplier));
	ForceNetUpdate();
	OnRep_Health(); // the server does not get the RepNotify
	UE_LOG(LogBHBreakable, Log, TEXT("%s hit by %s (x%.2f): %.1f / %.1f."), *GetNameSafe(this), *GetNameSafe(Attacker), DamageMultiplier, Health, MaxHealth);

	if (Health <= 0.f)
	{
		BreakNow();
	}
}

void ABH_Breakable::BreakNow()
{
	if (GetNetMode() == NM_Client || bBroken)
	{
		return;
	}
	Health = 0.f;
	bBroken = true;
	if (ABH_GameState* BHGameState = ABH_GameState::Get(this))
	{
		BHGameState->SetWorldFlag(GetResolvedBrokenFlag(), true); // reaches every client, in range or not
	}
	ForceNetUpdate();
	RefreshBrokenState(); // the server does not get the RepNotify
}

void ABH_Breakable::OnRep_Health()
{
	BP_OnHealthChanged(Health);
}

void ABH_Breakable::OnRep_Broken()
{
	RefreshBrokenState();
}

void ABH_Breakable::RefreshBrokenState()
{
	if (bAppliedBroken || !ComputeEffectiveBroken())
	{
		return;
	}
	bAppliedBroken = true;
	if (GetNetMode() == NM_Client)
	{
		Health = 0.f; // a client that returns from out of range still holds the level default
	}
	SetActorEnableCollision(false);
	if (BodyMesh)
	{
		// Takes the mesh out of the nav build; the navmesh only rebuilds the gap at runtime when it generates Dynamically.
		BodyMesh->SetCanEverAffectNavigation(false);
	}
	if (bHideWhenBroken)
	{
		SetActorHiddenInGame(true);
	}
	BP_OnBrokenChanged(true);
}
