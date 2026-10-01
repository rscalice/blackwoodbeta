// Blackwood Hollow - enemy wave spawner
// Target: Unreal Engine 5.8 (C++)
//
// Spawns waves of AI combatants (any pawn carrying a UBH_CombatIdentityComponent, e.g. the Vanguard Echo or the
// Corrupted Vanguard) for "Hold the Line" style encounters. Server only.
//   StartWaves -> Wave N: spawn entries one by one every SpawnDelay -> all dead -> OnWaveCleared
//   -> NextWaveDelay -> Wave N+1 ... -> OnAllWavesCleared (or the last wave repeats when bLoopLastWave).
// Spawned enemies are forced onto the Enemies team, never auto-reset on death, and are destroyed DespawnDelay seconds
// after dying. Spawn points are the SpawnPoints actors (round-robin) or, when empty, a ring of SpawnRadius around this
// actor. Each point is projected onto the navmesh (falling back to a floor trace) and lifted by the capsule half height,
// so the spawner / spawn point actors should sit at floor level.
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

/** One archetype inside a wave. */
USTRUCT(BlueprintType)
struct FBH_WaveEntry
{
	GENERATED_BODY()

	/** Must carry a UBH_CombatIdentityComponent (not ABH_EnemyBase: the Echo is a Blueprint character without that base). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Wave")
	TSubclassOf<APawn> EnemyClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Wave", meta = (ClampMin = "1"))
	int32 SpawnCount = 1;
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
	APawn* SpawnEnemy(TSubclassOf<APawn> EnemyClass, int32 SpawnIndex);
	FVector ResolveSpawnLocation(TSubclassOf<APawn> EnemyClass, int32 SpawnIndex, FRotator& OutRotation) const;
	void CheckWaveCleared();
	void AdvanceAfterClear();

	UFUNCTION()
	void HandleEnemyDeath(AActor* Killer);

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

	/** Classes still to spawn in the current wave (flattened entries). */
	TArray<TSubclassOf<APawn>> PendingSpawns;

	int32 CurrentWaveIndex = -1;
	int32 SpawnedInWave = 0;
	bool bRunning = false;

	FTimerHandle SpawnTimer;
	FTimerHandle NextWaveTimer;
};
