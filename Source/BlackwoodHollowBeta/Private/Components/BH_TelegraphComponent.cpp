// Blackwood Hollow - attack telegraph (ground warning) component (implementation)

#include "Components/BH_TelegraphComponent.h"
#include "Components/DecalComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

UBH_TelegraphComponent::UBH_TelegraphComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false; // only ticks while a telegraph is active
	SetIsReplicatedByDefault(true);
}

bool UBH_TelegraphComponent::ShouldSpawnVisuals() const
{
	const UWorld* World = GetWorld();
	return World && !World->IsNetMode(NM_DedicatedServer) && (!DecalMaterial.IsNull() || !NiagaraSystem.IsNull());
}

float UBH_TelegraphComponent::GetFillAlpha() const
{
	if (!bActive)
	{
		return LastFillAlpha;
	}
	const UWorld* World = GetWorld();
	if (!World || TelegraphDuration <= KINDA_SMALL_NUMBER)
	{
		return 1.f;
	}
	return FMath::Clamp(static_cast<float>((World->GetTimeSeconds() - StartTime) / TelegraphDuration), 0.f, 1.f);
}

float UBH_TelegraphComponent::GetTimeRemaining() const
{
	return bActive ? FMath::Max(TelegraphDuration * (1.f - GetFillAlpha()), 0.f) : 0.f;
}

void UBH_TelegraphComponent::StartTelegraph(FVector Location, float Radius, float Duration)
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}
	Multicast_StartTelegraph(FVector_NetQuantize(Location), Radius, Duration);
}

void UBH_TelegraphComponent::CancelTelegraph()
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}
	Multicast_CancelTelegraph();
}

void UBH_TelegraphComponent::Multicast_StartTelegraph_Implementation(FVector_NetQuantize Location, float Radius, float Duration)
{
	BeginLocal(Location, Radius, Duration);
}

void UBH_TelegraphComponent::Multicast_CancelTelegraph_Implementation()
{
	CancelLocal();
}

void UBH_TelegraphComponent::BeginLocal(const FVector& Location, float Radius, float Duration)
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	DestroyVisuals();
	bActive = true;
	StartTime = World->GetTimeSeconds();
	TelegraphLocation = Location;
	TelegraphRadius = FMath::Max(Radius, 0.f);
	TelegraphDuration = FMath::Max(Duration, 0.f);
	LastFillAlpha = 0.f;

	if (ShouldSpawnVisuals())
	{
		SpawnVisuals();
		UpdateVisuals(0.f);
	}

	SetComponentTickEnabled(true);
	OnTelegraphStarted.Broadcast(TelegraphLocation, TelegraphRadius, TelegraphDuration);
}

void UBH_TelegraphComponent::CancelLocal()
{
	bActive = false;
	LastFillAlpha = 0.f;
	SetComponentTickEnabled(false);
	DestroyVisuals();
}

void UBH_TelegraphComponent::FinishLocal()
{
	bActive = false;
	LastFillAlpha = 1.f;
	SetComponentTickEnabled(false);
	UpdateVisuals(1.f);

	if (UWorld* World = GetWorld())
	{
		if (CompleteLingerTime > 0.f && (DecalComp || NiagaraComp))
		{
			World->GetTimerManager().SetTimer(LingerTimer, this, &UBH_TelegraphComponent::DestroyVisuals, CompleteLingerTime, false);
		}
		else
		{
			DestroyVisuals();
		}
	}
	OnTelegraphComplete.Broadcast();
}

void UBH_TelegraphComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bActive)
	{
		return;
	}
	const float Alpha = GetFillAlpha();
	LastFillAlpha = Alpha;
	UpdateVisuals(Alpha);
	if (Alpha >= 1.f)
	{
		FinishLocal();
	}
}

void UBH_TelegraphComponent::SpawnVisuals()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (!DecalMaterial.IsNull())
	{
		if (UMaterialInterface* Material = DecalMaterial.LoadSynchronous())
		{
			// Decals project along their local X axis: pitch -90 points it straight down. DecalSize is a half extent.
			DecalComp = UGameplayStatics::SpawnDecalAtLocation(World, Material,
				FVector(DecalDepth, TelegraphRadius, TelegraphRadius), TelegraphLocation, FRotator(-90.f, 0.f, 0.f), /*LifeSpan*/ 0.f);
			if (DecalComp)
			{
				DecalMID = DecalComp->CreateDynamicMaterialInstance();
			}
		}
	}

	if (!NiagaraSystem.IsNull())
	{
		if (UNiagaraSystem* System = NiagaraSystem.LoadSynchronous())
		{
			NiagaraComp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, System, TelegraphLocation, FRotator::ZeroRotator,
				FVector::OneVector, /*bAutoDestroy*/ false, /*bAutoActivate*/ true);
			if (NiagaraComp && !RadiusParameterName.IsNone())
			{
				NiagaraComp->SetVariableFloat(RadiusParameterName, TelegraphRadius);
			}
		}
	}
}

void UBH_TelegraphComponent::UpdateVisuals(float Alpha)
{
	if (DecalMID && !FillAlphaParameterName.IsNone())
	{
		DecalMID->SetScalarParameterValue(FillAlphaParameterName, Alpha);
	}
	if (NiagaraComp && !FillAlphaParameterName.IsNone())
	{
		NiagaraComp->SetVariableFloat(FillAlphaParameterName, Alpha);
	}
}

void UBH_TelegraphComponent::DestroyVisuals()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LingerTimer);
	}
	if (DecalComp)
	{
		DecalComp->DestroyComponent();
		DecalComp = nullptr;
	}
	DecalMID = nullptr;
	if (NiagaraComp)
	{
		NiagaraComp->DestroyComponent();
		NiagaraComp = nullptr;
	}
}

void UBH_TelegraphComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bActive = false;
	DestroyVisuals();
	Super::EndPlay(EndPlayReason);
}
