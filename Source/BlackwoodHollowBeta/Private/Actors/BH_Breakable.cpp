// Blackwood Hollow - a breakable world blocker (implementation)

#include "Actors/BH_Breakable.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/EngineTypes.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogBHBreakable, Log, All);

ABH_Breakable::ABH_Breakable()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
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

void ABH_Breakable::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() != NM_Client)
	{
		Health = MaxHealth;
	}
	else if (bBroken)
	{
		ApplyBrokenState();
	}
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
	ForceNetUpdate();
	OnRep_Broken(); // the server does not get the RepNotify
}

void ABH_Breakable::OnRep_Health()
{
	BP_OnHealthChanged(Health);
}

void ABH_Breakable::OnRep_Broken()
{
	ApplyBrokenState();
	BP_OnBrokenChanged(bBroken);
}

void ABH_Breakable::ApplyBrokenState()
{
	if (!bBroken)
	{
		return;
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
}
