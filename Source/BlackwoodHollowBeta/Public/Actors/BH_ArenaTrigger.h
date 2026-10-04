// Blackwood Hollow - arena entrance trigger
// Target: Unreal Engine 5.8 (C++)
//
// A box volume placed in an arena doorway. When a player-controlled pawn walks in, it starts the wave spawners listed
// in Spawners (ABH_EnemyWaveSpawner::StartWaves), optionally after StartDelay seconds. Server only: the overlap is
// ignored on clients, and the spawners themselves are server-side logic.
//   - Only spawners that are not already running are started, so a second trigger (or a bAutoStart spawner) can't
//     restart a fight in progress.
//   - bTriggerOnce (default) latches the trigger and switches its collision off; ResetTrigger() re-arms it.
//   - bRequirePlayerControlled (default) ignores AI pawns wandering through the doorway.
//   - OnArenaTriggered (BlueprintAssignable) / K2_OnArenaTriggered fire at the moment the waves are started (after the
//     delay), with the pawn that tripped it: close doors, play a sting, lock the camera, etc.
// Pairs with ABH_LoadoutPad: pads listing the same spawners switch themselves off when the first wave starts.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/TimerHandle.h"
#include "BH_ArenaTrigger.generated.h"

class UBoxComponent;
class ABH_EnemyWaveSpawner;
class APawn;
class UPrimitiveComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBH_OnArenaTriggered, APawn*, TriggeringPawn);

UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API ABH_ArenaTrigger : public AActor
{
	GENERATED_BODY()

public:
	ABH_ArenaTrigger();

	// -- Config ---------------------------------------------------------------------------

	/** Spawners started when the trigger fires. Pick them in the level (instance only). */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "BH|Arena")
	TArray<TObjectPtr<ABH_EnemyWaveSpawner>> Spawners;

	/** Fire once, then disable the volume (ResetTrigger re-arms it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Arena")
	bool bTriggerOnce = true;

	/** Only pawns with a player controller can trip it (AI wandering in is ignored). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Arena")
	bool bRequirePlayerControlled = true;

	/** Seconds between the pawn entering and the spawners starting. 0 = immediately. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Arena", meta = (ClampMin = "0"))
	float StartDelay = 0.f;

	// -- Control (server) -----------------------------------------------------------------

	/** Re-arm after a bTriggerOnce fire: clears the latch and any pending delayed start, re-enables the volume. */
	UFUNCTION(BlueprintCallable, Category = "BH|Arena")
	void ResetTrigger();

	UFUNCTION(BlueprintPure, Category = "BH|Arena")
	bool HasTriggered() const { return bTriggered; }

	// -- Events ---------------------------------------------------------------------------

	/** Server only. The waves have just been started (after StartDelay). */
	UPROPERTY(BlueprintAssignable, Category = "BH|Arena")
	FBH_OnArenaTriggered OnArenaTriggered;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Blueprint hook, same moment as OnArenaTriggered. */
	UFUNCTION(BlueprintImplementableEvent, Category = "BH|Arena", meta = (DisplayName = "On Arena Triggered"))
	void K2_OnArenaTriggered(APawn* TriggeringPawn);

private:
	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	void FireTrigger(APawn* TriggeringPawn);

	UPROPERTY(VisibleAnywhere, Category = "BH|Arena")
	TObjectPtr<UBoxComponent> TriggerBox;

	bool bTriggered = false;
	FTimerHandle StartDelayTimer;
};
