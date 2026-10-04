// Blackwood Hollow - loadout pad (implementation)

#include "Actors/BH_LoadoutPad.h"
#include "AI/BH_EnemyWaveSpawner.h"
#include "Combat/BH_LoadoutComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextBlock.h"
#include "Components/WidgetComponent.h"
#include "Blueprint/UserWidget.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"

ABH_LoadoutPad::ABH_LoadoutPad()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false; // only the billboard needs it; enabled in BeginPlay where a label renders
	bReplicates = true;
	SetNetUpdateFrequency(2.f); // bPadActive changes rarely

	PadTrigger = CreateDefaultSubobject<UBoxComponent>(TEXT("PadTrigger"));
	SetRootComponent(PadTrigger);
	PadTrigger->SetBoxExtent(FVector(150.f, 150.f, 100.f));
	PadTrigger->SetCollisionProfileName(TEXT("Trigger"));
	PadTrigger->SetGenerateOverlapEvents(true);
	PadTrigger->ShapeColor = FColor(80, 160, 230);
	PadTrigger->SetHiddenInGame(true);

	PadMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PadMesh"));
	PadMesh->SetupAttachment(PadTrigger);
	PadMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PadMesh->SetGenerateOverlapEvents(false);
	PadMesh->SetCastShadow(false);

	AmbientFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("AmbientFX"));
	AmbientFX->SetupAttachment(PadTrigger);
	AmbientFX->SetAutoActivate(false); // pad FX only play when a player uses the pad

	LabelWidget = CreateDefaultSubobject<UWidgetComponent>(TEXT("LabelWidget"));
	LabelWidget->SetupAttachment(PadTrigger);
	LabelWidget->SetRelativeLocation(FVector(0.f, 0.f, 250.f));
	LabelWidget->SetWidgetSpace(EWidgetSpace::World);
	LabelWidget->SetDrawAtDesiredSize(true);
	LabelWidget->SetTwoSided(true);
	LabelWidget->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LabelWidget->SetGenerateOverlapEvents(false);
	LabelWidget->SetCastShadow(false);
}

void ABH_LoadoutPad::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABH_LoadoutPad, bPadActive);
}

void ABH_LoadoutPad::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// Convenience: the ambient asset is set per instance / BP child through AmbientFXAsset.
	if (AmbientFX && AmbientFXAsset && AmbientFX->GetAsset() != AmbientFXAsset)
	{
		AmbientFX->SetAsset(AmbientFXAsset);
	}
}

void ABH_LoadoutPad::BeginPlay()
{
	Super::BeginPlay();

	// The pad FX play on use only. Switch it off here too: instances / BP children may still carry bAutoActivate=true
	// (SetAutoActivate is not callable after construction, so this just deactivates).
	if (AmbientFX)
	{
		AmbientFX->Deactivate();
	}

	// Label: every machine writes its own copy of the text (the widget is not replicated).
	if (LabelWidget)
	{
		LabelWidget->InitWidget();
	}
	RefreshLabel();

	if (GetNetMode() != NM_DedicatedServer && LabelWidget)
	{
		SetActorTickEnabled(true);
	}

	if (HasAuthority())
	{
		if (PadTrigger)
		{
			PadTrigger->OnComponentBeginOverlap.AddDynamic(this, &ABH_LoadoutPad::HandleBeginOverlap);
		}

		bool bAlreadyRunning = false;
		for (ABH_EnemyWaveSpawner* Spawner : WaveSpawners)
		{
			if (IsValid(Spawner))
			{
				Spawner->OnWaveStarted.AddDynamic(this, &ABH_LoadoutPad::HandleWaveStarted);
				bAlreadyRunning |= Spawner->IsRunning();
			}
		}
		if (bDeactivateWhenWavesStart && bAlreadyRunning)
		{
			bPadActive = false; // the fight was already on when the level started
		}
	}

	ApplyPadActiveState();
}

void ABH_LoadoutPad::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (ABH_EnemyWaveSpawner* Spawner : WaveSpawners)
	{
		if (IsValid(Spawner))
		{
			Spawner->OnWaveStarted.RemoveDynamic(this, &ABH_LoadoutPad::HandleWaveStarted);
		}
	}
	Super::EndPlay(EndPlayReason);
}

// ============================================================================
// Label
// ============================================================================

FString ABH_LoadoutPad::StanceDisplayName(FName Stance)
{
	if (Stance.IsNone())
	{
		return TEXT("-");
	}
	if (Stance == FName(TEXT("DualSword")))
	{
		return TEXT("Dual Sword");
	}
	if (Stance == FName(TEXT("SwordAndShield")))
	{
		return TEXT("Sword & Shield");
	}
	return Stance.ToString();
}

void ABH_LoadoutPad::RefreshLabel()
{
	UUserWidget* Widget = LabelWidget ? LabelWidget->GetWidget() : nullptr;
	if (!Widget)
	{
		return; // no widget class assigned yet: nothing to write to
	}

	if (UTextBlock* LabelText = Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("LabelText"))))
	{
		LabelText->SetText(PresetDisplayName);
	}
	if (UTextBlock* SubText = Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("SubText"))))
	{
		// Derived from the item classes' default grip types (UBH_LoadoutComponent::GetStanceNameForPreset), not hard-coded.
		const FString Line = FString::Printf(TEXT("Set A: %s  |  Set B: %s"),
			*StanceDisplayName(UBH_LoadoutComponent::GetStanceNameForPreset(Preset, EBH_LoadoutSet::A)),
			*StanceDisplayName(UBH_LoadoutComponent::GetStanceNameForPreset(Preset, EBH_LoadoutSet::B)));
		SubText->SetText(FText::FromString(Line));
	}
}

void ABH_LoadoutPad::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!LabelWidget || !bPadActive)
	{
		return;
	}
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC || !PC->PlayerCameraManager)
	{
		return;
	}
	// Yaw-only billboard: the label stays upright and turns toward the local camera.
	FVector ToCamera = PC->PlayerCameraManager->GetCameraLocation() - LabelWidget->GetComponentLocation();
	ToCamera.Z = 0.f;
	if (!ToCamera.IsNearlyZero())
	{
		LabelWidget->SetWorldRotation(ToCamera.Rotation());
	}
}

// ============================================================================
// Active state
// ============================================================================

void ABH_LoadoutPad::SetPadActive(bool bActive)
{
	if (!HasAuthority() || bPadActive == bActive)
	{
		return;
	}
	bPadActive = bActive;
	ApplyPadActiveState(); // OnRep covers the clients; the server (and a listen host) applies directly
	ForceNetUpdate();
}

void ABH_LoadoutPad::OnRep_PadActive()
{
	ApplyPadActiveState();
}

void ABH_LoadoutPad::ApplyPadActiveState()
{
	if (PadTrigger)
	{
		PadTrigger->SetCollisionEnabled(bPadActive ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	}
	if (AmbientFX)
	{
		if (!bPadActive)
		{
			AmbientFX->Deactivate(); // never activated here: the pad FX only play on use (MulticastPlayApplyFX)
		}
	}
	if (LabelWidget)
	{
		LabelWidget->SetVisibility(bPadActive);
	}
	if (GetNetMode() != NM_DedicatedServer && LabelWidget)
	{
		SetActorTickEnabled(bPadActive);
	}
	K2_OnPadActiveChanged(bPadActive);
}

void ABH_LoadoutPad::HandleWaveStarted(int32 WaveIndex)
{
	if (bDeactivateWhenWavesStart)
	{
		UE_LOG(LogBHLoadout, Log, TEXT("LoadoutPad '%s': wave %d started, closing the pad"), *GetName(), WaveIndex);
		SetPadActive(false);
	}
}

// ============================================================================
// Step-on (server)
// ============================================================================

void ABH_LoadoutPad::HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!HasAuthority() || !bPadActive)
	{
		return;
	}
	APawn* Pawn = Cast<APawn>(OtherActor);
	if (!Pawn || !Pawn->IsPlayerControlled())
	{
		return;
	}

	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	for (auto It = LastUseTimes.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent(); // pawn went away: keep the map from growing
		}
	}
	if (const double* Last = LastUseTimes.Find(Pawn))
	{
		if (Now - *Last < ReuseCooldown)
		{
			return;
		}
	}
	LastUseTimes.Add(Pawn, Now); // stamped on every attempt so a failing apply doesn't spam the log either

	UBH_LoadoutComponent* Loadout = UBH_LoadoutComponent::FindLoadoutComponent(Pawn);
	if (!Loadout)
	{
		UE_LOG(LogBHLoadout, Warning, TEXT("LoadoutPad '%s': %s has no UBH_LoadoutComponent"), *GetName(), *GetNameSafe(Pawn));
		return;
	}

	FText Reason;
	if (Loadout->ApplyLoadoutPreset(Preset, Reason))
	{
		UE_LOG(LogBHLoadout, Log, TEXT("LoadoutPad '%s': applied '%s' to %s"), *GetName(), *PresetDisplayName.ToString(), *GetNameSafe(Pawn));
		MulticastPlayApplyFX(Pawn);
		OnLoadoutApplied.Broadcast(Pawn);
		K2_OnLoadoutApplied(Pawn);
	}
	else
	{
		UE_LOG(LogBHLoadout, Warning, TEXT("LoadoutPad '%s': could not apply '%s' to %s: %s"), *GetName(), *PresetDisplayName.ToString(), *GetNameSafe(Pawn), *Reason.ToString());
	}
}

void ABH_LoadoutPad::MulticastPlayApplyFX_Implementation(APawn* Pawn)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (Pawn && ApplyBurstFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, ApplyBurstFX, Pawn->GetActorLocation(), Pawn->GetActorRotation(),
			FVector::OneVector, /*bAutoDestroy*/ true, /*bAutoActivate*/ true);
	}

	// The pad's own effect plays once per use; a looping asset is switched off again after PadFXPlayTime.
	if (AmbientFX && AmbientFX->GetAsset())
	{
		AmbientFX->ResetSystem();
		AmbientFX->Activate(true);
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(PadFXStopTimer, this, &ABH_LoadoutPad::StopPadFX, FMath::Max(PadFXPlayTime, 0.1f), false);
		}
	}
}

void ABH_LoadoutPad::StopPadFX()
{
	if (AmbientFX)
	{
		AmbientFX->Deactivate();
	}
}
