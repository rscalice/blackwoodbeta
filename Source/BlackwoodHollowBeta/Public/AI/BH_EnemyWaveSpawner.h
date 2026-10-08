// Blackwood Hollow - enemy wave spawner
// Target: Unreal Engine 5.8 (C++)
//
// Spawns waves of AI combatants (an ABH_EnemyBase such as the crab, OR any pawn carrying a UBH_CombatIdentityComponent, e.g. the
// Vanguard Echo or the Corrupted Vanguard) for "Hold the Line" style encounters. Server only.
//   StartWaves -> Wave N: spawn entries one by one every SpawnDelay -> all dead -> OnWaveCleared
//   -> NextWaveDelay -> Wave N+1 ... -> OnAllWavesCleared (or the last wave repeats when bLoopLastWave).
// Spawned enemies are forced onto the Enemies team, never auto-reset on death, and are destroyed DespawnDelay seconds
// after dying. Spawn points are the SpawnPoints actors (round-robin) or, when empty, a ring of SpawnRadius around this
// actor. Each point is projected onto the navmesh (falling back to a floor trace) and lifted by the capsule half height,
// so the spawner / spawn point actors should sit at floor level.
// Phase 9: every wave entry has an EnemyLevel; it is written to ABH_EnemyBase::EnemyLevel BEFORE the deferred spawn finishes, so the
// level scaling (ApplyLevelScaling at BeginPlay) sees it. Entries that are not ABH_EnemyBase ignore the level.
// Debug (server): DebugSkipToWave / DebugSpawnBoss (console: bh.Arena.SkipToWave N, bh.Arena.SpawnBoss). The boss class is found by
// inspecting the entry classes for a UBH_CombatIdentityComponent template with bIsBoss.
// Note: the original brief called the wave struct FEnemyWave; project naming uses the BH_ prefix. A wave holds a list of
// entries (class + count) so a wave can mix archetypes.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/TimerHandle.h"
#include "BH_EnemyWaveSpawner.generated.h"

class UBillboardComponent;
class UArrowComponent;
class UBH_CombatIdentityComponent;
class ABH_EnemyBase;

/** One archetype inside a wave. */
USTRUCT(BlueprintType)
struct FBH_WaveEntry
{
	GENERATED_BODY()

	/** An ABH_EnemyBase (e.g. the crab), or a Blueprint character carrying a UBH_CombatIdentityComponent (the Echo has no ABH_EnemyBase base). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Wave")
	TSubclassOf<APawn> EnemyClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Wave", meta = (ClampMin = "1"))
	int32 SpawnCount = 1;

	/** Level given to every enemy of this entry (ABH_EnemyBase::EnemyLevel; drives the enemy scaling table rows). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Wave", meta = (ClampMin = "1"))
	int32 EnemyLevel = 1;
};

/** One enemy still to spawn in the current wave (flattened wave entry). */
struct FBH_PendingWaveSpawn
{
	TSubclassOf<APawn> EnemyClass;
	int32 EnemyLevel = 1;
};

/** One wave: its entries are spawned in order, SpawnDelay apart. */
USTRUCT(BlueprintType)
struct FBH_EnemyWave
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Wave")
	TArray<FBH_WaveEntry> Entries;

	/** Seconds between individual spawns inside the wave. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Wave", meta = (ClampMin = "0"))
	float SpawnDelay = 0.5f;

	/** Seconds after this wave is cleared before the next one starts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Wave", meta = (ClampMin = "0"))
	float NextWaveDelay = 3.f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBH_OnWaveStarted, int32, WaveIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBH_OnWaveCleared, int32, WaveIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FBH_OnAllWavesCleared);

UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API ABH_EnemyWaveSpawner : public AActor
{
	GENERATED_BODY()

public:
	ABH_EnemyWaveSpawner();

	// -- Config ---------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Waves")
	TArray<FBH_EnemyWave> Waves;

	/** Actors to spawn at (round-robin). Empty: ring of SpawnRadius around this actor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Waves")
	TArray<TObjectPtr<AActor>> SpawnPoints;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Waves", meta = (ClampMin = "0"))
	float SpawnRadius = 400.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Waves")
	bool bAutoStart = false;

	/** Seconds a dead enemy stays before it is destroyed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Waves", meta = (ClampMin = "0"))
	float DespawnDelay = 5.f;

	/** After the last wave is cleared, run it again instead of finishing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Waves")
	bool bLoopLastWave = false;

	// -- Control (server) -----------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "BH|Waves")
	void StartWaves();

	/** Stops spawning and wave progression; enemies already alive stay. */
	UFUNCTION(BlueprintCallable, Category = "BH|Waves")
	void StopWaves();

	/** Index of the running (or last run) wave, -1 before the first start. */
	UFUNCTION(BlueprintPure, Category = "BH|Waves")
	int32 GetCurrentWaveIndex() const { return CurrentWaveIndex; }

	UFUNCTION(BlueprintPure, Category = "BH|Waves")
	int32 GetAliveCount() const;

	UFUNCTION(BlueprintPure, Category = "BH|Waves")
	bool IsRunning() const { return bRunning; }

	// -- Debug (server; used by the bh.Arena.* console commands) --------------------------------

	/** Debug: abandons the current wave (living enemies are removed) and starts wave WaveNumber (1 = the first wave). */
	UFUNCTION(BlueprintCallable, Category = "BH|Waves|Debug")
	void DebugSkipToWave(int32 WaveNumber);

	/** Debug: spawns one boss now (the first wave entry whose class is flagged bIsBoss, searching the last wave first; if none is flagged, the last entry of the last wave). */
	UFUNCTION(BlueprintCallable, Category = "BH|Waves|Debug")
	void DebugSpawnBoss();

	/** The wave entry class that is a boss (CDO/SCS identity component with bIsBoss), or null when none of the entries is. */
	UFUNCTION(BlueprintPure, Category = "BH|Waves|Debug")
	TSubclassOf<APawn> FindBossClass() const;

	// -- Events ---------------------------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "BH|Waves")
	FBH_OnWaveStarted OnWaveStarted;

	UPROPERTY(BlueprintAssignable, Category = "BH|Waves")
	FBH_OnWaveCleared OnWaveCleared;

	UPROPERTY(BlueprintAssignable, Category = "BH|Waves")
	FBH_OnAllWavesCleared OnAllWavesCleared;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void BeginWave(int32 WaveIndex);
	void SpawnNext();
	APawn* SpawnEnemy(TSubclassOf<APawn> EnemyClass, int32 SpawnIndex, int32 EnemyLevel);
	FVector ResolveSpawnLocation(TSubclassOf<APawn> EnemyClass, int32 SpawnIndex, FRotator& OutRotation) const;
	void CheckWaveCleared();
	void AdvanceAfterClear();

	UFUNCTION()
	void HandleEnemyDeath(AActor* Killer);

	/** ABH_EnemyBase death (native delegate): same bookkeeping as an identity death. */
	void HandleEnemyBaseDeath(ABH_EnemyBase* Enemy, AActor* Killer);

	UFUNCTION()
	void HandleEnemyDestroyed(AActor* DestroyedActor);

	void ReleaseEnemy(APawn* Pawn);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

#if WITH_EDITORONLY_DATA
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UBillboardComponent> Billboard;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UArrowComponent> Arrow;
#endif

	UPROPERTY(Transient)
	TArray<TObjectPtr<APawn>> Alive;

	/** Enemies still to spawn in the current wave (flattened entries). */
	TArray<FBH_PendingWaveSpawn> PendingSpawns;

	int32 CurrentWaveIndex = -1;
	int32 SpawnedInWave = 0;
	bool bRunning = false;

	FTimerHandle SpawnTimer;
	FTimerHandle NextWaveTimer;
};
