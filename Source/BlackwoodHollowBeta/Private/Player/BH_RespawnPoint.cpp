// Blackwood Hollow - hub respawn points (implementation)

#include "Player/BH_RespawnPoint.h"
#include "AbilitySystem/StatusEffects/BH_FractureEffects.h"
#include "Loot/BH_LootLibrary.h"
#include "Player/BH_PlayerDeathComponent.h"
#include "Player/BH_PlayerState.h"
#include "NarrativeItem.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Components/ArrowComponent.h"
#include "Components/SceneComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "TimerManager.h"

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

	// Fracture repair (Phase 11G). No meshes on this actor, so no outline; the prompt is the whole feedback.
	RepairInteractable = CreateDefaultSubobject<UBH_InteractableComponent>(TEXT("RepairInteractable"));
	RepairInteractable->PromptName = NSLOCTEXT("BlackwoodHollow", "FractureRepairName", "Heart Fragment");
	RepairInteractable->PromptAction = NSLOCTEXT("BlackwoodHollow", "FractureRepairAction", "Repair Fracture");
	RepairInteractable->HoldSeconds = 1.5f;
	RepairInteractable->InteractRange = 300.f;
	RepairInteractable->bOutlineWhenFocused = false;
}

// ============================================================================
// Fracture repair
// ============================================================================

bool ABH_RespawnPoint::BH_CanInteract(const APawn* InteractingPawn, FText& OutDenyReason) const
{
	OutDenyReason = FText::GetEmpty();

	// Phase 12F: a point that does not fracture (the Wreck Camp) has nothing to repair either.
	if (!bAppliesFracture)
	{
		return false;
	}

	// Hidden entirely unless this player is Fractured.
	if (!InteractingPawn || !UBH_FractureLibrary::IsActorFractured(InteractingPawn))
	{
		return false;
	}

	const TSubclassOf<UNarrativeItem> ShardClass = UBH_LootLibrary::ResolveItemClass(TEXT("shard"));
	const int32 Owned = ShardClass ? UBH_LootLibrary::GetItemCount(InteractingPawn->GetPlayerState(), ShardClass) : 0;
	if (Owned < RepairShardCost)
	{
		OutDenyReason = NSLOCTEXT("BlackwoodHollow", "FractureRepairNotEnough", "Not enough shards");
		return false;
	}
	return true;
}

FText ABH_RespawnPoint::BH_GetPromptAction(const APawn* InteractingPawn) const
{
	const FText ShardName = UBH_LootLibrary::GetItemDisplayName(UBH_LootLibrary::ResolveItemClass(TEXT("shard")));
	return FText::Format(NSLOCTEXT("BlackwoodHollow", "FractureRepairPrompt", "Repair Fracture ({0} x {1})"),
		FText::AsNumber(RepairShardCost), ShardName);
}

void ABH_RespawnPoint::BH_OnInteractionCompleted(APawn* InteractingPawn)
{
	// Server only (the interactor calls this on the authority). Re-validate everything: the client only ever saw a replicated view.
	if (!InteractingPawn || !InteractingPawn->HasAuthority())
	{
		return;
	}
	FText Reason;
	if (!BH_CanInteract(InteractingPawn, Reason))
	{
		return;
	}

	APlayerState* const PlayerState = InteractingPawn->GetPlayerState();
	const TSubclassOf<UNarrativeItem> ShardClass = UBH_LootLibrary::ResolveItemClass(TEXT("shard"));
	if (!PlayerState || !ShardClass)
	{
		return;
	}

	const int32 Removed = UBH_LootLibrary::RemoveItemFromPlayer(PlayerState, ShardClass, RepairShardCost);
	if (Removed < RepairShardCost)
	{
		// Could not take the full price (a race with another consumer): give back what was taken, repair nothing.
		if (Removed > 0)
		{
			UBH_LootLibrary::GrantItem(PlayerState, ShardClass, Removed, /*bNotifyPlayer*/ false);
		}
		return;
	}

	UBH_FractureLibrary::RemoveFracture(UBH_FractureLibrary::ResolveASC(InteractingPawn));
}

bool ABH_RespawnPoint::IsAttunedFor(const APlayerState* PlayerState) const
{
	const ABH_PlayerState* BHState = Cast<ABH_PlayerState>(PlayerState);
	if (!BHState)
	{
		return false;
	}
	return bAttunedByDefault ? !BHState->AreStarterPointsRetired() : BHState->IsHubAttuned(HubName);
}

void ABH_RespawnPoint::BeginPlay()
{
	Super::BeginPlay();
	if (UBH_RespawnPointSubsystem* Registry = UBH_RespawnPointSubsystem::Get(this))
	{
		Registry->RegisterPoint(this);
	}

	// Attunement is server business (this actor is not replicated, so a client copy also reports HasAuthority: use the net mode).
	if (GetNetMode() != NM_Client && !bAttunedByDefault)
	{
		GetWorldTimerManager().SetTimer(AttuneTimer, this, &ABH_RespawnPoint::PollAttunement, 0.5f, true);
	}
}

void ABH_RespawnPoint::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(AttuneTimer);
	if (UBH_RespawnPointSubsystem* Registry = UBH_RespawnPointSubsystem::Get(this))
	{
		Registry->UnregisterPoint(this);
	}
	Super::EndPlay(EndPlayReason);
}

void ABH_RespawnPoint::PollAttunement()
{
	const UWorld* PointWorld = GetWorld();
	if (!PointWorld)
	{
		return;
	}
	const double RadiusSq = FMath::Square(static_cast<double>(AttuneRadius));
	for (FConstPlayerControllerIterator It = PointWorld->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* Controller = It->Get();
		const APawn* ControlledPawn = Controller ? Controller->GetPawn() : nullptr;
		ABH_PlayerState* BHState = Controller ? Controller->GetPlayerState<ABH_PlayerState>() : nullptr;
		if (!ControlledPawn || !BHState || !UBH_PlayerDeathComponent::IsLivingPlayer(ControlledPawn))
		{
			continue;
		}
		if (FVector::DistSquared(ControlledPawn->GetActorLocation(), GetActorLocation()) <= RadiusSq)
		{
			// A regular point also retires the starter camp for this player.
			BHState->AttuneHub(HubName, /*bRetireStarterPoints*/ true);
		}
	}
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

ABH_RespawnPoint* UBH_RespawnPointSubsystem::FindNearestForPlayer(const FVector& Location, const APlayerState* PlayerState) const
{
	ABH_RespawnPoint* BestAttuned = nullptr;
	double BestAttunedDistSq = TNumericLimits<double>::Max();
	for (const TWeakObjectPtr<ABH_RespawnPoint>& Weak : Points)
	{
		ABH_RespawnPoint* Point = Weak.Get();
		if (!Point || !Point->IsAttunedFor(PlayerState))
		{
			continue;
		}
		const double DistSq = FVector::DistSquared(Point->GetActorLocation(), Location);
		if (DistSq < BestAttunedDistSq)
		{
			BestAttunedDistSq = DistSq;
			BestAttuned = Point;
		}
	}
	return BestAttuned ? BestAttuned : FindNearest(Location);
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
