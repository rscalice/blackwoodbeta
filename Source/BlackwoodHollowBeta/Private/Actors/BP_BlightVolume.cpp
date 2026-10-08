// Blackwood Hollow - Blight Volume Actor (implementation)

#include "Actors/BP_BlightVolume.h"
#include "Components/BoxComponent.h"
#include "Components/BPC_HeartFragment.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"

ABP_BlightVolume::ABP_BlightVolume()
{
	PrimaryActorTick.bCanEverTick = false; // build-up is timer-driven, not per-frame
	bReplicates = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	OverlapVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("OverlapVolume"));
	OverlapVolume->SetupAttachment(Root);
	OverlapVolume->SetBoxExtent(FVector(500.f, 500.f, 200.f));
	OverlapVolume->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	OverlapVolume->SetGenerateOverlapEvents(true);
}

void ABP_BlightVolume::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABP_BlightVolume, bCleared);
}

void ABP_BlightVolume::BeginPlay()
{
	Super::BeginPlay();

	// Build-up is server-side only: clients never track overlaps (they only see bCleared).
	if (!HasAuthority())
	{
		return;
	}

	OverlapVolume->OnComponentBeginOverlap.AddDynamic(this, &ABP_BlightVolume::OnVolumeBeginOverlap);
	OverlapVolume->OnComponentEndOverlap.AddDynamic(this, &ABP_BlightVolume::OnVolumeEndOverlap);

	// Characters that were already standing in the volume when it began play have no begin-overlap event for our delegate.
	TSet<UPrimitiveComponent*> Existing;
	OverlapVolume->GetOverlappingComponents(Existing);
	for (UPrimitiveComponent* Comp : Existing)
	{
		AActor* OverlapOwner = Comp ? Comp->GetOwner() : nullptr;
		if (OverlapOwner && OverlapOwner != this && IsCharacterCapsule(OverlapOwner, Comp))
		{
			OverlappingActors.Add(OverlapOwner);
		}
	}

	GetWorldTimerManager().SetTimer(BuildupTickTimerHandle, this, &ABP_BlightVolume::BuildupTick, FMath::Max(TickInterval, 0.05f), true);
}

void ABP_BlightVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(BuildupTickTimerHandle);
	GetWorldTimerManager().ClearTimer(RegrowTimerHandle);

	Super::EndPlay(EndPlayReason);
}

bool ABP_BlightVolume::IsCharacterCapsule(const AActor* Actor, const UPrimitiveComponent* Comp)
{
	const ACharacter* Character = Cast<ACharacter>(Actor);
	return Character && Comp && Comp == Character->GetCapsuleComponent();
}

void ABP_BlightVolume::OnVolumeBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!OtherActor || OtherActor == this || !IsCharacterCapsule(OtherActor, OtherComp))
	{
		return;
	}
	OverlappingActors.Add(OtherActor); // a set: harmless even if the capsule re-enters
}

void ABP_BlightVolume::OnVolumeEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex)
{
	if (!OtherActor || !IsCharacterCapsule(OtherActor, OtherComp))
	{
		return;
	}
	OverlappingActors.Remove(OtherActor);
}

void ABP_BlightVolume::BuildupTick()
{
	if (bCleared || BuildupPerSecond <= 0.f)
	{
		return;
	}

	const float Amount = BuildupPerSecond * FMath::Max(TickInterval, 0.05f);

	// Copy first: AddBlightBuildup can saturate the meter, which fires gameplay events whose handlers may change the overlap set.
	const TArray<TWeakObjectPtr<AActor>> ActorsToTick = OverlappingActors.Array();
	for (const TWeakObjectPtr<AActor>& WeakActor : ActorsToTick)
	{
		AActor* TargetActor = WeakActor.Get();
		if (!TargetActor)
		{
			OverlappingActors.Remove(WeakActor);
			continue;
		}
		if (!UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(TargetActor))
		{
			continue;
		}
		// Only characters with a Heart-Fragment can build up Blight (enemies do not).
		if (UBPC_HeartFragment* HeartFragment = TargetActor->FindComponentByClass<UBPC_HeartFragment>())
		{
			HeartFragment->AddBlightBuildup(Amount, this);
		}
	}
}

// ============================================================================
// Clearing
// ============================================================================

void ABP_BlightVolume::ClearFog()
{
	if (!HasAuthority())
	{
		return;
	}

	if (!bCleared)
	{
		bCleared = true;
		OnRep_Cleared(); // the server / listen-server host does not get the RepNotify
		ForceNetUpdate();
	}

	if (bClearPermanently)
	{
		GetWorldTimerManager().ClearTimer(RegrowTimerHandle);
	}
	else
	{
		// Clearing again while already clear pushes the regrow back.
		GetWorldTimerManager().SetTimer(RegrowTimerHandle, this, &ABP_BlightVolume::RegrowFog, FMath::Max(RegrowSeconds, 0.1f), false);
	}
}

void ABP_BlightVolume::RegrowFog()
{
	if (!HasAuthority() || bClearPermanently || !bCleared)
	{
		return;
	}
	bCleared = false;
	OnRep_Cleared();
	ForceNetUpdate();
}

void ABP_BlightVolume::OnRep_Cleared()
{
	OnFogClearedChanged(bCleared);
}

bool ABP_BlightVolume::IntersectsSphere(const FVector& Center, float Radius) const
{
	if (!OverlapVolume)
	{
		return false;
	}

	// Closest point of the (rotated, scaled) box to the sphere centre, computed in the box's local space.
	const FTransform BoxTransform(OverlapVolume->GetComponentRotation(), OverlapVolume->GetComponentLocation());
	const FVector LocalCenter = BoxTransform.InverseTransformPosition(Center);
	const FVector Extent = OverlapVolume->GetScaledBoxExtent();
	const FVector Closest(
		FMath::Clamp(LocalCenter.X, -Extent.X, Extent.X),
		FMath::Clamp(LocalCenter.Y, -Extent.Y, Extent.Y),
		FMath::Clamp(LocalCenter.Z, -Extent.Z, Extent.Z));
	return FVector::DistSquared(LocalCenter, Closest) <= FMath::Square(Radius);
}
