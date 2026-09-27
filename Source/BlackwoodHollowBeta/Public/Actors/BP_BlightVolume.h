// Blackwood Hollow - Blight Volume Actor
// Target: Unreal Engine 5.8 (C++)
//
// A placeable hazard volume representing pooled Blight fog (used across the
// Blighted Coastal Crabs arena in Wreckage Shallows and beyond). Ticks a
// damage-over-time to overlapping actors, routes that damage through
// UBPC_HeartFragment's Blight shield first, and temporarily clears/suppresses
// itself in response to a nearby Overload Burst.
//
// Naming note: kept as "BP_" per the brief even though this is the native
// C++ base class -- treat it as the class Blueprint children (if any) are
// expected to extend, consistent with how this project names its volume
// actors (see BP_FireSmokeVolume in project notes).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BP_BlightVolume.generated.h"

class UBoxComponent;
class UBPC_HeartFragment;
class UAbilitySystemComponent;

UCLASS()
class BLACKWOODHOLLOWBETA_API ABP_BlightVolume : public AActor
{
	GENERATED_BODY()

public:
	ABP_BlightVolume();

	virtual void Tick(float DeltaTime) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	// -- Components -----------------------------------------------------------

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BlightVolume")
	TObjectPtr<USceneComponent> Root;

	/** Overlap volume defining the Blight fog's extent. Scale/shape in the editor per placement. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BlightVolume")
	TObjectPtr<UBoxComponent> OverlapVolume;

	// -- DoT tuning -----------------------------------------------------------

	/** Raw Blight damage per tick, before BlightResistance / Heart-Fragment shielding. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlightVolume|Damage")
	float DamagePerTick = 5.f;

	/** Seconds between damage ticks for each overlapping actor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlightVolume|Damage")
	float TickInterval = 1.f;

	/** If true, this volume is currently suppressed (e.g. by a nearby Overload Burst) and deals no damage. */
	UPROPERTY(BlueprintReadOnly, Category = "BlightVolume|State")
	bool bSuppressed = false;

	/** How long an Overload Burst suppresses this volume for, in seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlightVolume|OverloadResponse")
	float OverloadSuppressionDuration = 6.f;

	/** Radius (world units) within which an Overload Burst on any actor will suppress this volume. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlightVolume|OverloadResponse")
	float OverloadResponseRadius = 1500.f;

protected:
	UFUNCTION()
	void OnVolumeBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnVolumeEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex);

	/** Applies one tick of Blight damage to a single overlapping actor, checking for a UBPC_HeartFragment first. */
	void ApplyBlightTickToActor(AActor* TargetActor);

	/** Bound to Event.Combat.OverloadBurst on any ASC within OverloadResponseRadius (see SubscribeToOverloadEvents). */
	void HandleOverloadBurstNearby(AActor* BurstInstigator);

	/** Called on a timer while bSuppressed is true; clears suppression after OverloadSuppressionDuration. */
	void OnSuppressionExpired();

private:
	UPROPERTY(Transient)
	TSet<TWeakObjectPtr<AActor>> OverlappingActors;

	FTimerHandle DamageTickTimerHandle;
	FTimerHandle SuppressionTimerHandle;

	/** Cached delegate handles for gameplay-event subscriptions added in BeginPlay, removed in EndPlay. */
	TArray<TPair<TWeakObjectPtr<UAbilitySystemComponent>, FDelegateHandle>> OverloadEventSubscriptions;

	void DamageTick();

	/** Finds nearby ability system components (e.g. via overlap sphere) and subscribes to their Overload Burst event. */
	void SubscribeToOverloadEvents();
	void UnsubscribeFromOverloadEvents();
};
