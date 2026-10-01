// Blackwood Hollow - enemy wave spawner (implementation)

#include "AI/BH_EnemyWaveSpawner.h"
#include "AI/BH_AIController.h"
#include "Combat/BH_CombatIdentityComponent.h"
#include "Combat/BH_CombatTeam.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "Components/ArrowComponent.h"
#include "Components/BillboardComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "NavigationSystem.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "CollisionQueryParams.h"

ABH_EnemyWaveSpawner::ABH_EnemyWaveSpawner()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false; // server-side logic only; spawned pawns replicate themselves

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

#if WITH_EDITORONLY_DATA
	Billboard = CreateDefaultSubobject<UBillboardComponent>(TEXT("Billboard"));
	Billboard->SetupAttachment(Root);
	Billboard->bIsScreenSizeScaled = true;

	Arrow = CreateDefaultSubobject<UArrowComponent>(TEXT("Arrow"));
	Arrow->SetupAttachment(Root);
	Arrow->ArrowColor = FColor(170, 20, 70);
	Arrow->ArrowSize = 1.5f;
	Arrow->bIsScreenSizeScaled = true;
#endif
}

void ABH_EnemyWaveSpawner::BeginPlay()
{
	Super::BeginPlay();
	if (bAutoStart && HasAuthority())
	{
		StartWaves();
	}
}

void ABH_EnemyWaveSpawner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SpawnTimer);
		World->GetTimerManager().ClearTimer(NextWaveTimer);
	}
	Super::EndPlay(EndPlayReason);
}

// ============================================================================
// Control
// ============================================================================

void ABH_EnemyWaveSpawner::StartWaves()
{
	if (!HasAuthority())
	{
		return;
	}
	if (Waves.IsEmpty())
	{
		UE_LOG(LogBHCombat, Warning, TEXT("WaveSpawner '%s': StartWaves with no waves configured."), *GetName());
		return;
	}
	StopWaves();
	bRunning = true;
	BeginWave(0);
}

void ABH_EnemyWaveSpawner::StopWaves()
{
	bRunning = false;
	PendingSpawns.Reset();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SpawnTimer);
		World->GetTimerManager().ClearTimer(NextWaveTimer);
	}
}

int32 ABH_EnemyWaveSpawner::GetAliveCount() const
{
	return Alive.Num();
}

// ============================================================================
// Waves
// ============================================================================

void ABH_EnemyWaveSpawner::BeginWave(int32 WaveIndex)
{
	if (!bRunning || !Waves.IsValidIndex(WaveIndex))
	{
		return;
	}
	CurrentWaveIndex = WaveIndex;
	SpawnedInWave = 0;

	const FBH_EnemyWave& Wave = Waves[WaveIndex];
	PendingSpawns.Reset();
	for (const FBH_WaveEntry& Entry : Wave.Entries)
	{
		for (int32 i = 0; i < Entry.SpawnCount; ++i)
		{
			PendingSpawns.Add(Entry.EnemyClass);
		}
	}

	UE_LOG(LogBHCombat, Log, TEXT("WaveSpawner '%s': wave %d started (%d enemies)."), *GetName(), WaveIndex, PendingSpawns.Num());
	OnWaveStarted.Broadcast(WaveIndex);

	SpawnNext(); // the first enemy appears immediately; the rest follow every SpawnDelay
	if (bRunning && CurrentWaveIndex == WaveIndex && !PendingSpawns.IsEmpty())
	{
		GetWorldTimerManager().SetTimer(SpawnTimer, this, &ABH_EnemyWaveSpawner::SpawnNext, FMath::Max(0.01f, Wave.SpawnDelay), true);
	}
}

void ABH_EnemyWaveSpawner::SpawnNext()
{
	if (!bRunning)
	{
		return;
	}
	if (PendingSpawns.IsEmpty())
	{
		GetWorldTimerManager().ClearTimer(SpawnTimer);
		CheckWaveCleared();
		return;
	}

	const TSubclassOf<APawn> EnemyClass = PendingSpawns[0];
	PendingSpawns.RemoveAt(0);
	SpawnEnemy(EnemyClass, SpawnedInWave);
	++SpawnedInWave;

	if (PendingSpawns.IsEmpty())
	{
		GetWorldTimerManager().ClearTimer(SpawnTimer);
		CheckWaveCleared();
	}
}

FVector ABH_EnemyWaveSpawner::ResolveSpawnLocation(TSubclassOf<APawn> EnemyClass, int32 SpawnIndex, FRotator& OutRotation) const
{
	FVector Base = GetActorLocation();
	OutRotation = GetActorRotation();

	TArray<AActor*> ValidPoints;
	for (AActor* Point : SpawnPoints)
	{
		if (IsValid(Point))
		{
			ValidPoints.Add(Point);
		}
	}
	if (!ValidPoints.IsEmpty())
	{
		const AActor* Point = ValidPoints[SpawnIndex % ValidPoints.Num()];
		Base = Point->GetActorLocation();
		OutRotation = Point->GetActorRotation();
	}
	else
	{
		// Ring around the spawner: evenly spread by spawn index, enemies face inward (toward the arena centre).
		const int32 Total = FMath::Max(1, SpawnedInWave + PendingSpawns.Num() + 1);
		const float Angle = 2.f * PI * (static_cast<float>(SpawnIndex) / Total) + GetActorRotation().Yaw * PI / 180.f;
		const FVector Offset(FMath::Cos(Angle) * SpawnRadius, FMath::Sin(Angle) * SpawnRadius, 0.f);
		Base += Offset;
		OutRotation = FRotator(0.f, (-Offset).Rotation().Yaw, 0.f);
	}

	float HalfHeight = 90.f;
	if (const ACharacter* CDO = EnemyClass ? Cast<ACharacter>(EnemyClass->GetDefaultObject()) : nullptr)
	{
		if (const UCapsuleComponent* Capsule = CDO->GetCapsuleComponent())
		{
			HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
		}
	}

	FVector Floor = Base;
	bool bFoundFloor = false;
	if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		FNavLocation NavLoc;
		if (Nav->ProjectPointToNavigation(Base, NavLoc, FVector(150.f, 150.f, 400.f)))
		{
			Floor = NavLoc.Location;
			bFoundFloor = true;
		}
	}
	if (!bFoundFloor)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(BH_WaveSpawnFloor), false, this);
		if (GetWorld()->LineTraceSingleByChannel(Hit, Base + FVector(0, 0, 300.f), Base - FVector(0, 0, 600.f), ECC_Visibility, Params))
		{
			Floor = Hit.ImpactPoint;
		}
	}
	return Floor + FVector(0.f, 0.f, HalfHeight + 2.f);
}

APawn* ABH_EnemyWaveSpawner::SpawnEnemy(TSubclassOf<APawn> EnemyClass, int32 SpawnIndex)
{
	UWorld* World = GetWorld();
	if (!World || !EnemyClass)
	{
		UE_LOG(LogBHCombat, Warning, TEXT("WaveSpawner '%s': wave %d has an entry with no EnemyClass; skipped."), *GetName(), CurrentWaveIndex);
		return nullptr;
	}

	FRotator Rotation;
	const FVector Location = ResolveSpawnLocation(EnemyClass, SpawnIndex, Rotation);

	APawn* Pawn = World->SpawnActorDeferred<APawn>(EnemyClass, FTransform(Rotation, Location), this, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!Pawn)
	{
		UE_LOG(LogBHCombat, Warning, TEXT("WaveSpawner '%s': failed to spawn %s."), *GetName(), *GetNameSafe(EnemyClass));
		return nullptr;
	}
	Pawn->FinishSpawning(FTransform(Rotation, Location));

	// The identity component is a Blueprint (SCS) component: it only exists once construction ran inside FinishSpawning.
	UBH_CombatIdentityComponent* Identity = UBH_CombatIdentityComponent::Find(Pawn);
	if (!Identity)
	{
		UE_LOG(LogBHCombat, Warning, TEXT("WaveSpawner '%s': %s has no UBH_CombatIdentityComponent; it cannot be tracked and was destroyed."), *GetName(), *GetNameSafe(EnemyClass));
		Pawn->Destroy();
		return nullptr;
	}

	Identity->CombatTeam = EBH_CombatTeam::Enemies;
	Identity->SetResetOnDeath(false);
	if (ABH_AIController* Brain = Cast<ABH_AIController>(Pawn->GetController()))
	{
		Brain->SetGenericTeamId(BH_CombatTeam::ToGenericTeamId(EBH_CombatTeam::Enemies));
	}

	Identity->OnDeath.AddDynamic(this, &ABH_EnemyWaveSpawner::HandleEnemyDeath);
	Pawn->OnDestroyed.AddDynamic(this, &ABH_EnemyWaveSpawner::HandleEnemyDestroyed);
	Alive.Add(Pawn);

	UE_LOG(LogBHCombat, Log, TEXT("WaveSpawner '%s': wave %d spawned %s at %s (alive %d)."), *GetName(), CurrentWaveIndex, *Pawn->GetName(), *Location.ToCompactString(), Alive.Num());
	return Pawn;
}

void ABH_EnemyWaveSpawner::HandleEnemyDeath(AActor* /*Killer*/)
{
	// The dynamic delegate does not say who died: sweep for pawns whose identity reports dead.
	TArray<APawn*> Dead;
	for (APawn* Pawn : Alive)
	{
		const UBH_CombatIdentityComponent* Identity = UBH_CombatIdentityComponent::Find(Pawn);
		if (!IsValid(Pawn) || !Identity || Identity->IsDead())
		{
			Dead.Add(Pawn);
		}
	}
	for (APawn* Pawn : Dead)
	{
		ReleaseEnemy(Pawn);
		if (IsValid(Pawn))
		{
			Pawn->SetLifeSpan(FMath::Max(0.01f, DespawnDelay));
		}
	}
	CheckWaveCleared();
}

void ABH_EnemyWaveSpawner::HandleEnemyDestroyed(AActor* DestroyedActor)
{
	if (APawn* Pawn = Cast<APawn>(DestroyedActor))
	{
		if (Alive.Contains(Pawn))
		{
			ReleaseEnemy(Pawn);
			CheckWaveCleared();
		}
	}
}

void ABH_EnemyWaveSpawner::ReleaseEnemy(APawn* Pawn)
{
	Alive.Remove(Pawn);
	if (IsValid(Pawn))
	{
		Pawn->OnDestroyed.RemoveDynamic(this, &ABH_EnemyWaveSpawner::HandleEnemyDestroyed);
		if (UBH_CombatIdentityComponent* Identity = UBH_CombatIdentityComponent::Find(Pawn))
		{
			Identity->OnDeath.RemoveDynamic(this, &ABH_EnemyWaveSpawner::HandleEnemyDeath);
		}
	}
	UE_LOG(LogBHCombat, Log, TEXT("WaveSpawner '%s': %s down (alive %d)."), *GetName(), *GetNameSafe(Pawn), Alive.Num());
}

void ABH_EnemyWaveSpawner::CheckWaveCleared()
{
	if (!bRunning || !Alive.IsEmpty() || !PendingSpawns.IsEmpty() || !Waves.IsValidIndex(CurrentWaveIndex))
	{
		return;
	}
	if (GetWorldTimerManager().IsTimerActive(NextWaveTimer))
	{
		return; // already cleared, waiting for the next wave
	}

	const int32 Cleared = CurrentWaveIndex;
	UE_LOG(LogBHCombat, Log, TEXT("WaveSpawner '%s': wave %d cleared."), *GetName(), Cleared);
	OnWaveCleared.Broadcast(Cleared);
	if (!bRunning) // a listener may have stopped us
	{
		return;
	}

	const bool bHasNext = Waves.IsValidIndex(Cleared + 1);
	if (bHasNext || bLoopLastWave)
	{
		GetWorldTimerManager().SetTimer(NextWaveTimer, this, &ABH_EnemyWaveSpawner::AdvanceAfterClear, FMath::Max(0.01f, Waves[Cleared].NextWaveDelay), false);
	}
	else
	{
		bRunning = false;
		UE_LOG(LogBHCombat, Log, TEXT("WaveSpawner '%s': all waves cleared."), *GetName());
		OnAllWavesCleared.Broadcast();
	}
}

void ABH_EnemyWaveSpawner::AdvanceAfterClear()
{
	const int32 Next = Waves.IsValidIndex(CurrentWaveIndex + 1) ? CurrentWaveIndex + 1 : CurrentWaveIndex; // loop re-runs the last wave
	BeginWave(Next);
}
