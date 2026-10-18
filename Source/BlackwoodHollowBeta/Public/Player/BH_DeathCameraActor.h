// Blackwood Hollow - local dead camera (Phase 13I-2)
//
// While the local player lies in the ragdoll, the GASP gameplay camera (which hangs off the capsule root) would sit ~10-20 cm above the floor and
// its boom would end inside the floor or the corpse. This actor is the view target instead: a spring arm that orbits the own pelvis body with the
// Look input. It is local only (never replicated, transient), spawned by UBH_PlayerDeathComponent::EnterDeathLocal and destroyed after the blend back.
// Spectating a partner was cancelled by design (2026-10-17): the camera always stays on the player's own body.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BH_DeathCameraActor.generated.h"

class UCameraComponent;
class USceneComponent;
class USpringArmComponent;
class UBH_PlayerDeathComponent;
class APlayerController;

UCLASS(NotBlueprintable, Transient, NotPlaceable)
class BLACKWOODHOLLOWBETA_API ABH_DeathCameraActor : public AActor
{
	GENERATED_BODY()

public:
	ABH_DeathCameraActor();

	/** Binds the camera to the dying player's death component (pelvis source), its controller (orbit rotation) and the pivot height above the body. */
	void Initialize(UBH_PlayerDeathComponent* InDeathComponent, APlayerController* InController, float InPivotRaise);

	/** The pivot the camera orbits: the pelvis body (else the owner's location) raised by the pivot height, kept above the floor. */
	FVector ComputePivot() const;

	virtual void Tick(float DeltaSeconds) override;

	/** Spring arm length (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BH|DeathCamera", meta = (ClampMin = "50.0", ForceUnits = "cm"))
	float ArmLength = 320.f;

	/** Probe sphere radius of the arm collision test (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BH|DeathCamera", meta = (ClampMin = "1.0", ForceUnits = "cm"))
	float ProbeRadius = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BH|DeathCamera", meta = (ClampMin = "0.0"))
	float PositionLagSpeed = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BH|DeathCamera", meta = (ClampMin = "0.0"))
	float RotationLagSpeed = 14.f;

	/** The pivot never goes lower than this above the floor found by the down-trace (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BH|DeathCamera", meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float MinFloorClearance = 40.f;

	/** Length of the down-trace used to find the floor under the pivot (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BH|DeathCamera", meta = (ClampMin = "10.0", ForceUnits = "cm"))
	float FloorTraceDistance = 250.f;

private:
	UPROPERTY()
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY()
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY()
	TObjectPtr<UCameraComponent> Camera;

	TWeakObjectPtr<UBH_PlayerDeathComponent> DeathComponent;
	TWeakObjectPtr<APlayerController> Controller;
	float PivotRaise = 70.f;
	FVector LastPivot = FVector::ZeroVector;
};
