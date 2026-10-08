// Blackwood Hollow - hub respawn points (implementation)

#include "Player/BH_RespawnPoint.h"
#include "Components/ArrowComponent.h"
#include "Components/SceneComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"

ABH_RespawnPoint::ABH_RespawnPoint()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetCanBeDamaged(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	FacingArrow = CreateDefaultSubobject<UArrowComponent>(TEXT("FacingArrow"));
	FacingArrow->SetupAttachment(SceneRoot);
	FacingArrow->SetRelativeLocation(FVector(0.f, 0.f, 90.f));
	FacingArrow->ArrowSize = 2.f;
	FacingArrow->ArrowColor = FColor(80, 200, 255);
	FacingArrow->SetHiddenInGame(true);
}

void ABH_RespawnPoint::BeginPlay()
{
	Super::BeginPlay();
	if (UBH_RespawnPointSubsystem* Registry = UBH_RespawnPointSubsystem::Get(this))
	{
		Registry->RegisterPoint(this);
	}
}

void ABH_RespawnPoint::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UBH_RespawnPointSubsystem* Registry = UBH_RespawnPointSubsystem::Get(this))
	{
		Registry->UnregisterPoint(this);
	}
	Super::EndPlay(EndPlayReason);
}

FVector ABH_RespawnPoint::ComputeSpawnLocation(float CapsuleHalfHeight, int32 SlotIndex) const
{
	FVector Base = GetActorLocation();

	// Slot 0 stands on the point; the rest on a ring around it (four spots, then it wraps).
	if (SlotIndex > 0 && SpawnSpreadRadius > 0.f)
	{
		const float AngleRad = FMath::DegreesToRadians(90.f * static_cast<float>((SlotIndex - 1) % 4) + 45.f);
		const FVector Offset = GetActorRotation().RotateVector(FVector(FMath::Cos(AngleRad), FMath::Sin(AngleRad), 0.f) * SpawnSpreadRadius);
		Base += Offset;
	}

	// Snap to the floor so a point hovering a little above the ground (or on a slope) still drops the capsule onto it.
	float FloorZ = GetActorLocation().Z;
	if (const UWorld* World = GetWorld())
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(BHRespawnPointGround), false, this);
		FHitResult Hit;
		const FVector Start = Base + FVector(0.f, 0.f, GroundTraceDistance);
		const FVector End = Base - FVector(0.f, 0.f, GroundTraceDistance);
		if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
		{
			FloorZ = Hit.ImpactPoint.Z;
		}
	}
	return FVector(Base.X, Base.Y, FloorZ + CapsuleHalfHeight + 2.f);
}

// ============================================================================
// Registry
// ============================================================================

bool UBH_RespawnPointSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	// Only the server resolves respawn destinations.
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->GetNetMode() != NM_Client;
}

UBH_RespawnPointSubsystem* UBH_RespawnPointSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UBH_RespawnPointSubsystem>() : nullptr;
}

void UBH_RespawnPointSubsystem::RegisterPoint(ABH_RespawnPoint* Point)
{
	if (Point)
	{
		Points.AddUnique(Point);
	}
}

void UBH_RespawnPointSubsystem::UnregisterPoint(ABH_RespawnPoint* Point)
{
	Points.Remove(Point);
}

ABH_RespawnPoint* UBH_RespawnPointSubsystem::FindNearest(const FVector& Location) const
{
	ABH_RespawnPoint* Best = nullptr;
	double BestDistSq = TNumericLimits<double>::Max();
	for (const TWeakObjectPtr<ABH_RespawnPoint>& Weak : Points)
	{
		ABH_RespawnPoint* Point = Weak.Get();
		if (!Point)
		{
			continue;
		}
		const double DistSq = FVector::DistSquared(Point->GetActorLocation(), Location);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = Point;
		}
	}
	return Best;
}
