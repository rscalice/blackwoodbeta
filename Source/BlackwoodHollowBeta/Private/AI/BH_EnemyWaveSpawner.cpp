// Blackwood Hollow - enemy wave spawner (implementation)

#include "AI/BH_EnemyWaveSpawner.h"
#include "AI/BH_AIController.h"
#include "Combat/BH_CombatIdentityComponent.h"
#include "Characters/BH_EnemyBase.h"
#include "Combat/BH_CombatTeam.h"
#include "Player/BH_PartyStateSubsystem.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "Components/ArrowComponent.h"
#include "Components/BillboardComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "NavigationSystem.h"
#include "Engine/World.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "TimerManager.h"
#include "CollisionQueryParams.h"

namespace BH_WaveSpawner_Private
{
	/**
	 * True when EnemyClass is a Blueprint class whose UBH_CombatIdentityComponent template has bIsBoss. The component is added in the
	 * Blueprint (SCS), so it is read from the construction script of every Blueprint class in the chain, resolved against the most
	 * derived class so a child Blueprint's override of bIsBoss wins.
	 */
	static bool IsBossClass(UClass* EnemyClass)
	{
		UBlueprintGeneratedClass* Leaf = Cast<UBlueprintGeneratedClass>(EnemyClass);
		if (!Leaf)
		{
			return false;
		}
		for (UClass* Class = EnemyClass; Class; Class = Class->GetSuperClass())
		{
			const UBlueprintGeneratedClass* BPClass = Cast<UBlueprintGeneratedClass>(Class);
			if (!BPClass || !BPClass->SimpleConstructionScript)
			{
				continue;
			}
			for (const USCS_Node* Node : BPClass->SimpleConstructionScript->GetAllNodes())
			{
				if (!Node || !Node->ComponentClass || !Node->ComponentClass->IsChildOf(UBH_CombatIdentityComponent::StaticClass()))
				{
					continue;
				}
				const UBH_CombatIdentityComponent* Template = Cast<UBH_CombatIdentityComponent>(Node->GetActualComponentTemplate(Leaf));
				if (Template && Template->bIsBoss)
				{
					return true;
				}
			}
		}
		return false;
	}
}

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
	if (HasAuthority())
	{
		if (UBH_PartyStateSubsystem* PartyState = UBH_PartyStateSubsystem::Get(this))
		{
			PartyState->OnPartyWiped.AddDynamic(this, &ABH_EnemyWaveSpawner::HandlePartyWiped);
		}
	}
	if (bAutoStart && HasAuthority())
	{
		StartWaves();
	}
}

void ABH_EnemyWaveSpawner::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UBH_PartyStateSubsystem* PartyState = UBH_PartyStateSubsystem::Get(this))
	{
		PartyState->OnPartyWiped.RemoveDynamic(this, &ABH_EnemyWaveSpawner::HandlePartyWiped);
	}
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
// Debug
// ============================================================================

void ABH_EnemyWaveSpawner::DebugSkipToWave(int32 WaveNumber)
{
	if (!HasAuthority())
	{
		return;
	}
	const int32 WaveIndex = WaveNumber - 1;
	if (!Waves.IsValidIndex(WaveIndex))
	{
		UE_LOG(LogBHCombat, Warning, TEXT("WaveSpawner '%s': SkipToWave %d is out of range (1..%d)."), *GetName(), WaveNumber, Waves.Num());
		return;
	}

	StopWaves();

	// Clear the field: untrack first (so nothing counts as a death), then remove.
	const TArray<TObjectPtr<APawn>> Living = Alive;
	for (APawn* Pawn : Living)
	{
		ReleaseEnemy(Pawn);
		if (IsValid(Pawn))
		{
			Pawn->Destroy();
		}
	}
	Alive.Reset();

	UE_LOG(LogBHCombat, Log, TEXT("WaveSpawner '%s': debug skip to wave %d."), *GetName(), WaveNumber);
	bRunning = true;
	BeginWave(WaveIndex);
}

TSubclassOf<APawn> ABH_EnemyWaveSpawner::FindBossClass() const
{
	for (int32 WaveIndex = Waves.Num() - 1; WaveIndex >= 0; --WaveIndex)
	{
		for (const FBH_WaveEntry& Entry : Waves[WaveIndex].Entries)
		{
			if (Entry.EnemyClass && BH_WaveSpawner_Private::IsBossClass(Entry.EnemyClass.Get()))
			{
				return Entry.EnemyClass;
			}
		}
	}
	return nullptr;
}

void ABH_EnemyWaveSpawner::DebugSpawnBoss()
{
	if (!HasAuthority())
	{
		return;
	}
	TSubclassOf<APawn> BossClass = FindBossClass();
	if (!BossClass)
	{
		// Nothing is flagged bIsBoss: the boss is, by convention, the last entry of the last wave that has entries.
		for (int32 WaveIndex = Waves.Num() - 1; WaveIndex >= 0 && !BossClass; --WaveIndex)
		{
			for (int32 EntryIndex = Waves[WaveIndex].Entries.Num() - 1; EntryIndex >= 0 && !BossClass; --EntryIndex)
			{
				BossClass = Waves[WaveIndex].Entries[EntryIndex].EnemyClass;
			}
		}
		UE_LOG(LogBHCombat, Warning, TEXT("WaveSpawner '%s': no wave entry has bIsBoss on its identity component; falling back to the last entry of the last wave (%s)."), *GetName(), *GetNameSafe(BossClass));
	}
	if (!BossClass)
	{
		UE_LOG(LogBHCombat, Warning, TEXT("WaveSpawner '%s': SpawnBoss has no wave entries to take a boss from."), *GetName());
		return;
	}
	SpawnEnemy(BossClass, SpawnedInWave, 1);
	++SpawnedInWave;
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
	Roster.Reset();

	const FBH_EnemyWave& Wave = Waves[WaveIndex];
	PendingSpawns.Reset();
	for (const FBH_WaveEntry& Entry : Wave.Entries)
	{
		for (int32 i = 0; i < Entry.SpawnCount; ++i)
		{
			FBH_PendingWaveSpawn Pending;
			Pending.EnemyClass = Entry.EnemyClass;
			Pending.EnemyLevel = FMath::Max(1, Entry.EnemyLevel);
			PendingSpawns.Add(Pending);
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

	const FBH_PendingWaveSpawn Next = PendingSpawns[0];
	PendingSpawns.RemoveAt(0);
	SpawnEnemy(Next.EnemyClass, SpawnedInWave, Next.EnemyLevel);
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

APawn* ABH_EnemyWaveSpawner::SpawnEnemy(TSubclassOf<APawn> EnemyClass, int32 SpawnIndex, int32 EnemyLevel)
{
	if (!GetWorld() || !EnemyClass)
	{
		UE_LOG(LogBHCombat, Warning, TEXT("WaveSpawner '%s': wave %d has an entry with no EnemyClass; skipped."), *GetName(), CurrentWaveIndex);
		return nullptr;
	}

	FRotator Rotation;
	const FVector Location = ResolveSpawnLocation(EnemyClass, SpawnIndex, Rotation);

	FBH_WaveRosterSlot Slot;
	Slot.EnemyClass = EnemyClass;
	Slot.EnemyLevel = FMath::Max(1, EnemyLevel);
	Slot.SpawnTransform = FTransform(Rotation, Location);
	Slot.Pawn = SpawnEnemyAt(Slot.EnemyClass, Slot.SpawnTransform, Slot.EnemyLevel);
	APawn* Spawned = Slot.Pawn.Get();
	if (Spawned)
	{
		Roster.Add(Slot);
	}
	return Spawned;
}

APawn* ABH_EnemyWaveSpawner::SpawnEnemyAt(TSubclassOf<APawn> EnemyClass, const FTransform& SpawnTransform, int32 EnemyLevel)
{
	UWorld* World = GetWorld();
	if (!World || !EnemyClass)
	{
		return nullptr;
	}

	const FVector Location = SpawnTransform.GetLocation();
	const FRotator Rotation = SpawnTransform.Rotator();

	APawn* Pawn = World->SpawnActorDeferred<APawn>(EnemyClass, FTransform(Rotation, Location), this, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!Pawn)
	{
		UE_LOG(LogBHCombat, Warning, TEXT("WaveSpawner '%s': failed to spawn %s."), *GetName(), *GetNameSafe(EnemyClass));
		return nullptr;
	}

	// Native enemies (the crab) take their level and team BEFORE BeginPlay runs inside FinishSpawning, so ApplyLevelScaling sees them.
	ABH_EnemyBase* EnemyBase = Cast<ABH_EnemyBase>(Pawn);
	if (EnemyBase)
	{
		EnemyBase->EnemyLevel = FMath::Max(1, EnemyLevel);
		EnemyBase->CombatTeam = EBH_CombatTeam::Enemies;
		EnemyBase->bResetOnDeath = false;
	}
	Pawn->FinishSpawning(FTransform(Rotation, Location));

	// Two kinds of enemy are tracked: an ABH_EnemyBase (native death delegate), or a Blueprint character with a UBH_CombatIdentityComponent
	// (which only exists once construction ran inside FinishSpawning). Anything else cannot report its death.
	UBH_CombatIdentityComponent* Identity = EnemyBase ? nullptr : UBH_CombatIdentityComponent::Find(Pawn);
	if (!EnemyBase && !Identity)
	{
		UE_LOG(LogBHCombat, Warning, TEXT("WaveSpawner '%s': %s is neither an ABH_EnemyBase nor has a UBH_CombatIdentityComponent; it cannot be tracked and was destroyed."), *GetName(), *GetNameSafe(EnemyClass));
		Pawn->Destroy();
		return nullptr;
	}

	if (Identity)
	{
		Identity->CombatTeam = EBH_CombatTeam::Enemies;
		Identity->OnDeath.AddDynamic(this, &ABH_EnemyWaveSpawner::HandleEnemyDeath);
	}
	else
	{
		EnemyBase->OnEnemyDeath.AddUObject(this, &ABH_EnemyWaveSpawner::HandleEnemyBaseDeath);
	}
	if (ABH_AIController* Brain = Cast<ABH_AIController>(Pawn->GetController()))
	{
		Brain->SetGenericTeamId(BH_CombatTeam::ToGenericTeamId(EBH_CombatTeam::Enemies));
	}

	Pawn->OnDestroyed.AddDynamic(this, &ABH_EnemyWaveSpawner::HandleEnemyDestroyed);
	Alive.Add(Pawn);

	UE_LOG(LogBHCombat, Log, TEXT("WaveSpawner '%s': wave %d spawned %s (level %d) at %s (alive %d)."), *GetName(), CurrentWaveIndex, *Pawn->GetName(), EnemyBase ? EnemyBase->EnemyLevel : 0, *Location.ToCompactString(), Alive.Num());
	return Pawn;
}

void ABH_EnemyWaveSpawner::HandlePartyWiped()
{
	if (!HasAuthority() || !bRunning || !Waves.IsValidIndex(CurrentWaveIndex))
	{
		return;
	}
	// The wave was cleared and the next one is on its timer: a cleared wave stays cleared.
	if (GetWorldTimerManager().IsTimerActive(NextWaveTimer))
	{
		return;
	}

	int32 Respawned = 0;
	for (FBH_WaveRosterSlot& Slot : Roster)
	{
		APawn* Existing = Slot.Pawn.Get();
		if (Existing && IsValid(Existing) && Alive.Contains(Existing))
		{
			continue; // still alive: its own UBH_EnemyResetComponent healed it and sent it home
		}
		if (APawn* Fresh = SpawnEnemyAt(Slot.EnemyClass, Slot.SpawnTransform, Slot.EnemyLevel))
		{
			Slot.Pawn = Fresh;
			++Respawned;
		}
	}
	UE_LOG(LogBHCombat, Log, TEXT("WaveSpawner '%s': party wiped, wave %d restarted (%d respawned, %d alive, %d still to spawn)."), *GetName(), CurrentWaveIndex, Respawned, Alive.Num(), PendingSpawns.Num());
}

void ABH_EnemyWaveSpawner::HandleEnemyBaseDeath(ABH_EnemyBase* /*Enemy*/, AActor* Killer)
{
	HandleEnemyDeath(Killer);
}

void ABH_EnemyWaveSpawner::HandleEnemyDeath(AActor* /*Killer*/)
{
	// The delegates do not say who died: sweep for pawns that report dead.
	TArray<APawn*> Dead;
	for (APawn* Pawn : Alive)
	{
		if (!IsValid(Pawn))
		{
			Dead.Add(Pawn);
			continue;
		}
		const ABH_EnemyBase* EnemyBase = Cast<ABH_EnemyBase>(Pawn);
		const UBH_CombatIdentityComponent* Identity = UBH_CombatIdentityComponent::Find(Pawn);
		if (EnemyBase ? EnemyBase->IsEnemyDead() : (!Identity || Identity->IsDead()))
		{
			Dead.Add(Pawn);
		}
	}
	for (APawn* Pawn : Dead)
	{
		ReleaseEnemy(Pawn);
		// An enemy that already scheduled its own despawn (the crab's ragdoll timer) keeps it.
		if (IsValid(Pawn) && Pawn->GetLifeSpan() <= 0.f)
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
		if (ABH_EnemyBase* EnemyBase = Cast<ABH_EnemyBase>(Pawn))
		{
			EnemyBase->OnEnemyDeath.RemoveAll(this);
		}
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
