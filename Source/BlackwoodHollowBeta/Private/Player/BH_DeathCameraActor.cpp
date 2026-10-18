// Blackwood Hollow - local dead camera (Phase 13I-2, implementation)

#include "Player/BH_DeathCameraActor.h"
#include "Player/BH_PlayerDeathComponent.h"
#include "Camera/CameraComponent.h"
#include "CollisionQueryParams.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"

ABH_DeathCameraActor::ABH_DeathCameraActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics; // read the ragdoll bodies after the simulation step
	bReplicates = false; // local only
	SetCanBeDamaged(false);
	SetActorEnableCollision(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(SceneRoot);
	SpringArm->TargetArmLength = ArmLength;
	SpringArm->bDoCollisionTest = true;
	SpringArm->ProbeChannel = ECC_Camera;
	SpringArm->ProbeSize = ProbeRadius;
	SpringArm->bUsePawnControlRotation = false; // the rotation is set from the controller in Tick
	SpringArm->bEnableCameraLag = true;
	SpringArm->CameraLagSpeed = PositionLagSpeed;
	SpringArm->bEnableCameraRotationLag = true;
	SpringArm->CameraRotationLagSpeed = RotationLagSpeed;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
	Camera->bUsePawnControlRotation = false;
}

void ABH_DeathCameraActor::Initialize(UBH_PlayerDeathComponent* InDeathComponent, APlayerController* InController, const float InPivotRaise)
{
	DeathComponent = InDeathComponent;
	Controller = InController;
	PivotRaise = InPivotRaise;

	// Apply the (possibly edited) tunables, then start exactly at the body looking where the player looks: no lag sweep from the origin.
	SpringArm->TargetArmLength = ArmLength;
	SpringArm->ProbeSize = ProbeRadius;
	SpringArm->CameraLagSpeed = PositionLagSpeed;
	SpringArm->CameraRotationLagSpeed = RotationLagSpeed;

	LastPivot = ComputePivot();
	SetActorLocation(LastPivot, false, nullptr, ETeleportType::TeleportPhysics);
	if (const APlayerController* PC = Controller.Get())
	{
		SpringArm->SetWorldRotation(PC->GetControlRotation());
	}
}

FVector ABH_DeathCameraActor::ComputePivot() const
{
	const UBH_PlayerDeathComponent* Death = DeathComponent.Get();
	if (!Death)
	{
		return LastPivot; // the body is gone: hold the last pivot until the actor is destroyed
	}

	FVector BodyLocation = Death->GetBodyRestLocation(); // the owner's location when the pelvis has no body
	FVector PelvisVelocity = FVector::ZeroVector;
	FQuat PelvisRotation = FQuat::Identity;
	FVector PelvisLocation = FVector::ZeroVector;
	if (Death->GetPelvisState(PelvisLocation, PelvisVelocity, PelvisRotation))
	{
		BodyLocation = PelvisLocation;
	}

	FVector Pivot = BodyLocation + FVector(0.f, 0.f, PivotRaise);

	// Keep the pivot above the floor even when the body lies in a dip or the physics body sinks a little.
	const UWorld* World = GetWorld();
	if (World)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(BHDeathCameraFloor), false, Death->GetOwner());
		Params.AddIgnoredActor(this);
		FHitResult Hit;
		const FVector Start = BodyLocation + FVector(0.f, 0.f, 20.f);
		const FVector End = Start - FVector(0.f, 0.f, FloorTraceDistance);
		if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
		{
			Pivot.Z = FMath::Max(Pivot.Z, Hit.ImpactPoint.Z + MinFloorClearance);
		}
	}
	return Pivot;
}

void ABH_DeathCameraActor::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	LastPivot = ComputePivot();
	SetActorLocation(LastPivot, false, nullptr, ETeleportType::TeleportPhysics);

	// The player orbits with Look: the arm follows the control rotation (the spring arm's own rotation lag smooths it).
	if (const APlayerController* PC = Controller.Get())
	{
		SpringArm->SetWorldRotation(PC->GetControlRotation());
	}
}
