// Blackwood Hollow - Blight Volume Actor
// Target: Unreal Engine 5.8 (C++)
//
// A placeable hazard volume representing pooled Blight fog (used across the
// Blighted Coastal Crabs arena in Wreckage Shallows and beyond).
//
// Phase 10B: the fog is a pure BUILD-UP SOURCE for the Blight meter. It deals no damage and never touches Health: a server timer
// adds BuildupPerSecond * TickInterval to every character standing inside it through UBPC_HeartFragment::AddBlightBuildup
// (which applies BlightResistance, the Heart-Fragment shielding and the Aegis hook, and owns decay / saturation / Blight Rot).
// Actors without a Heart-Fragment (enemies) are ignored.
//
// Overlaps are tracked by the character CAPSULE only (ACharacter::GetCapsuleComponent), so a character with several overlapping
// components (mesh, weapon, shield...) is added and removed exactly once.
//
// Overload Burst calls ClearFog() directly (the old BeginPlay event-subscription scheme is gone). A cleared volume adds nothing
// and regrows after RegrowSeconds, or stays cleared forever when bClearPermanently is set. bCleared replicates; clients get
// OnFogClearedChanged so fog VFX (Niagara / material) can fade out and back in.
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
class UPrimitiveComponent;

UCLASS()
class BLACKWOODHOLLOWBETA_API ABP_BlightVolume : public AActor
{
	GENERATED_BODY()

public:
	ABP_BlightVolume();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

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

	// -- Build-up tuning ------------------------------------------------------

	/** Raw Blight build-up per second for everyone inside, before BlightResistance / shielding / Aegis. Standard fog 8; heavy fog 18. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlightVolume|Buildup", meta = (ClampMin = "0.0"))
	float BuildupPerSecond = 8.f;

	/** Seconds between build-up ticks (each tick adds BuildupPerSecond * TickInterval). Keep it well under the meter's decay delay (2 s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlightVolume|Buildup", meta = (ClampMin = "0.05", ForceUnits = "s"))
	float TickInterval = 0.25f;

	// -- Clearing (Overload Burst) ---------------------------------------------

	/** true: once cleared the fog never comes back. false: it regrows after RegrowSeconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlightVolume|Clearing")
	bool bClearPermanently = false;

	/** Seconds a cleared volume stays clear before it regrows (ignored when bClearPermanently). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlightVolume|Clearing", meta = (ClampMin = "0.1", ForceUnits = "s"))
	float RegrowSeconds = 6.f;

	/** True while the fog is cleared (no build-up). Replicated; clients react in OnFogClearedChanged. */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Cleared, Category = "BlightVolume|Clearing")
	bool bCleared = false;

	/** SERVER. Clears the fog now (Overload Burst). Starts the regrow timer unless bClearPermanently. Safe to call repeatedly (re-arms the regrow timer). */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "BlightVolume|Clearing")
	void ClearFog();

	UFUNCTION(BlueprintPure, Category = "BlightVolume|Clearing")
	bool IsFogCleared() const { return bCleared; }

	/** True if the sphere (Center, Radius) touches this volume's box (exact, respects the volume's rotation and scale). */
	UFUNCTION(BlueprintPure, Category = "BlightVolume")
	bool IntersectsSphere(const FVector& Center, float Radius) const;

protected:
	/** Fires on the server AND every client whenever bCleared changes: fade the fog VFX out (true) / back in (false). */
	UFUNCTION(BlueprintImplementableEvent, Category = "BlightVolume|Clearing", meta = (DisplayName = "On Fog Cleared Changed"))
	void OnFogClearedChanged(bool bNewCleared);

	UFUNCTION()
	void OnVolumeBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnVolumeEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex);

private:
	UFUNCTION()
	void OnRep_Cleared();

	/** Server timer: adds BuildupPerSecond * TickInterval to every tracked character. */
	void BuildupTick();

	/** Regrow timer: the fog comes back. */
	void RegrowFog();

	/** True if Comp is the capsule of a character (the only component type that counts as "inside"). */
	static bool IsCharacterCapsule(const AActor* Actor, const UPrimitiveComponent* Comp);

	UPROPERTY(Transient)
	TSet<TWeakObjectPtr<AActor>> OverlappingActors;

	FTimerHandle BuildupTickTimerHandle;
	FTimerHandle RegrowTimerHandle;
};
