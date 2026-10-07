// Blackwood Hollow - attack telegraph (ground warning) component
// Target: Unreal Engine 5.8 (C++)
//
// Lives on an attacker (ABH_EnemyCrab has one). The server calls StartTelegraph(Location, Radius, Duration); every machine
// then runs the same timer (a reliable multicast carries the start), so GetFillAlpha() goes 0 -> 1 over Duration locally and
// OnTelegraphComplete fires on every machine when it reaches 1. Logic only: the visuals are optional and empty-safe.
//   * DecalMaterial  - spawned as a ground decal (projects down) sized to Radius; the scalar material parameter FillAlphaParameterName
//                      ("FillAlpha") is driven 0..1 every tick.
//   * NiagaraSystem  - spawned at Location; the float user parameters FillAlphaParameterName and RadiusParameterName are driven.
// Nothing is spawned on a dedicated server. With neither asset set the component is a pure timer (still useful for gameplay).

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TimerManager.h"
#include "BH_TelegraphComponent.generated.h"

class UDecalComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UNiagaraComponent;
class UNiagaraSystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FBH_OnTelegraphStarted, FVector, Location, float, Radius, float, Duration);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FBH_OnTelegraphComplete);

UCLASS(ClassGroup = (BlackwoodHollow), meta = (BlueprintSpawnableComponent))
class BLACKWOODHOLLOWBETA_API UBH_TelegraphComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBH_TelegraphComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// -- Visual assets (all optional) -----------------------------------------------------------

	/** Ground decal material. Must expose a scalar parameter named FillAlphaParameterName. Empty = no decal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telegraph|Visuals")
	TSoftObjectPtr<UMaterialInterface> DecalMaterial;

	/** Optional Niagara system spawned at the telegraph location. Empty = none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telegraph|Visuals")
	TSoftObjectPtr<UNiagaraSystem> NiagaraSystem;

	/** Material scalar / Niagara float parameter that receives the 0..1 fill. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telegraph|Visuals")
	FName FillAlphaParameterName = TEXT("FillAlpha");

	/** Niagara float parameter that receives the radius (None = not set). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telegraph|Visuals")
	FName RadiusParameterName = TEXT("Radius");

	/** Decal projection depth (cm, half extent along the projection axis). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telegraph|Visuals", meta = (ClampMin = "1"))
	float DecalDepth = 200.f;

	/** Seconds the visuals stay after the fill completes (the flash of the "strike" moment). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Telegraph|Visuals", meta = (ClampMin = "0"))
	float CompleteLingerTime = 0.15f;

	// -- Events -----------------------------------------------------------------------------------

	/** Fires on every machine when a telegraph begins. */
	UPROPERTY(BlueprintAssignable, Category = "Telegraph")
	FBH_OnTelegraphStarted OnTelegraphStarted;

	/** Fires on every machine when the fill reaches 1 (not when cancelled). */
	UPROPERTY(BlueprintAssignable, Category = "Telegraph")
	FBH_OnTelegraphComplete OnTelegraphComplete;

	// -- API --------------------------------------------------------------------------------------

	/** AUTHORITY ONLY. Starts (or restarts) a telegraph at Location and tells every client. Radius in cm, Duration in seconds. */
	UFUNCTION(BlueprintCallable, Category = "Telegraph")
	void StartTelegraph(FVector Location, float Radius, float Duration);

	/** AUTHORITY ONLY. Stops the telegraph everywhere without firing OnTelegraphComplete. */
	UFUNCTION(BlueprintCallable, Category = "Telegraph")
	void CancelTelegraph();

	/** 0 -> 1 over Duration while active; stays at 1 after completion until the next start, 0 if never started. */
	UFUNCTION(BlueprintPure, Category = "Telegraph")
	float GetFillAlpha() const;

	UFUNCTION(BlueprintPure, Category = "Telegraph")
	bool IsTelegraphActive() const { return bActive; }

	UFUNCTION(BlueprintPure, Category = "Telegraph")
	FVector GetTelegraphLocation() const { return TelegraphLocation; }

	UFUNCTION(BlueprintPure, Category = "Telegraph")
	float GetTelegraphRadius() const { return TelegraphRadius; }

	UFUNCTION(BlueprintPure, Category = "Telegraph")
	float GetTelegraphDuration() const { return TelegraphDuration; }

	UFUNCTION(BlueprintPure, Category = "Telegraph")
	float GetTimeRemaining() const;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_StartTelegraph(FVector_NetQuantize Location, float Radius, float Duration);

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_CancelTelegraph();

private:
	void BeginLocal(const FVector& Location, float Radius, float Duration);
	void CancelLocal();
	void SpawnVisuals();
	void UpdateVisuals(float Alpha);
	void DestroyVisuals();
	void FinishLocal();
	bool ShouldSpawnVisuals() const;

	bool bActive = false;
	double StartTime = 0.0;
	float TelegraphDuration = 0.f;
	float TelegraphRadius = 0.f;
	float LastFillAlpha = 0.f;
	FVector TelegraphLocation = FVector::ZeroVector;

	UPROPERTY(Transient)
	TObjectPtr<UDecalComponent> DecalComp;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DecalMID;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> NiagaraComp;

	FTimerHandle LingerTimer;
};
