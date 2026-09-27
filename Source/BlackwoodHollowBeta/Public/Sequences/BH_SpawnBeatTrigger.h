// Blackwood Hollow - Spawn-beat Level Sequence trigger
// Target: Unreal Engine 5.8 (C++)
//
// Reusable, placeable trigger for scripted opening beats -- built first for
// "Among the Wrack and Ruin", the initial spawn beat in the Wreckage
// Shallows level setup, but written generically so it covers later
// intro/cutscene beats on Islands 2 and 3 too.
//
// Place one instance in /Game/Levels/Prototype (or the Wreckage Shallows
// sub-level), assign SpawnBeatSequence to the "Among the Wrack and Ruin"
// Level Sequence asset, and size OverlapVolume to the player's spawn/landing
// area. By default it fires once, disables player input for the duration
// (configurable), and re-enables it on completion or skip.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BH_SpawnBeatTrigger.generated.h"

class UBoxComponent;
class ULevelSequence;
class ULevelSequencePlayer;
class UMovieSceneSequencePlaybackSettings;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSpawnBeatFinished);

UCLASS()
class BLACKWOODHOLLOWBETA_API ABH_SpawnBeatTrigger : public AActor
{
	GENERATED_BODY()

public:
	ABH_SpawnBeatTrigger();

protected:
	virtual void BeginPlay() override;

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SpawnBeatTrigger")
	TObjectPtr<USceneComponent> Root;

	/** Overlap volume that fires the beat when the local player enters it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SpawnBeatTrigger")
	TObjectPtr<UBoxComponent> OverlapVolume;

	/** The Level Sequence asset to play -- e.g. LS_AmongTheWrackAndRuin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnBeatTrigger")
	TSoftObjectPtr<ULevelSequence> SpawnBeatSequence;

	/** Human-readable beat name, purely for logging/debugging (e.g. "Among the Wrack and Ruin"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnBeatTrigger")
	FString BeatName = TEXT("Among the Wrack and Ruin");

	/** If true, fires automatically the first time a player-controlled pawn overlaps. If false, call PlaySpawnBeat() manually (e.g. from a level Blueprint's BeginPlay, for a beat that should run immediately on level load). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnBeatTrigger")
	bool bTriggerOnOverlap = true;

	/** If true, disables the triggering pawn's movement/look input for the duration of the sequence. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnBeatTrigger")
	bool bDisablePlayerInputDuringBeat = true;

	/** If true, this trigger only ever fires once per level instance (typical for a spawn/intro beat). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SpawnBeatTrigger")
	bool bOneShot = true;

	UPROPERTY(BlueprintReadOnly, Category = "SpawnBeatTrigger")
	bool bHasFired = false;

	/** Plays the assigned sequence immediately, regardless of overlap. Safe to call from Blueprint (e.g. a level-load spawn beat) or C++. */
	UFUNCTION(BlueprintCallable, Category = "SpawnBeatTrigger")
	void PlaySpawnBeat(APawn* InstigatingPawn);

	/** Skips the currently-playing beat, jumping to its end and restoring player input. Wire to a "Skip Cutscene" input action. */
	UFUNCTION(BlueprintCallable, Category = "SpawnBeatTrigger")
	void SkipSpawnBeat();

	UPROPERTY(BlueprintAssignable, Category = "SpawnBeatTrigger")
	FOnSpawnBeatFinished OnSpawnBeatFinished;

protected:
	UFUNCTION()
	void OnVolumeBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void HandleSequenceFinished();

private:
	UPROPERTY(Transient)
	TObjectPtr<ULevelSequencePlayer> ActiveSequencePlayer;

	UPROPERTY(Transient)
	TWeakObjectPtr<APawn> PawnUnderInputLock;

	void SetPlayerInputLocked(APawn* Pawn, bool bLocked);
};
