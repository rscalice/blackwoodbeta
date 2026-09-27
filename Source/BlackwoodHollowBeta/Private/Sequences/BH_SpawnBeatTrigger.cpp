// Blackwood Hollow - Spawn-beat Level Sequence trigger (implementation)

#include "Sequences/BH_SpawnBeatTrigger.h"
#include "Components/BoxComponent.h"
#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "MovieSceneSequencePlaybackSettings.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

ABH_SpawnBeatTrigger::ABH_SpawnBeatTrigger()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	OverlapVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("OverlapVolume"));
	OverlapVolume->SetupAttachment(Root);
	OverlapVolume->SetBoxExtent(FVector(300.f, 300.f, 200.f));
	OverlapVolume->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	OverlapVolume->SetGenerateOverlapEvents(true);
}

void ABH_SpawnBeatTrigger::BeginPlay()
{
	Super::BeginPlay();

	if (bTriggerOnOverlap)
	{
		OverlapVolume->OnComponentBeginOverlap.AddDynamic(this, &ABH_SpawnBeatTrigger::OnVolumeBeginOverlap);
	}
}

void ABH_SpawnBeatTrigger::OnVolumeBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	APawn* Pawn = Cast<APawn>(OtherActor);
	if (!Pawn || !Pawn->IsLocallyControlled())
	{
		return;
	}

	PlaySpawnBeat(Pawn);
}

void ABH_SpawnBeatTrigger::PlaySpawnBeat(APawn* InstigatingPawn)
{
	if (bOneShot && bHasFired)
	{
		return;
	}

	ULevelSequence* Sequence = SpawnBeatSequence.LoadSynchronous();
	if (!Sequence)
	{
		UE_LOG(LogTemp, Warning, TEXT("ABH_SpawnBeatTrigger '%s': no SpawnBeatSequence assigned for beat '%s'."), *GetName(), *BeatName);
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	bHasFired = true;

	FMovieSceneSequencePlaybackSettings PlaybackSettings;
	PlaybackSettings.bAutoPlay = false;
	PlaybackSettings.bPauseAtEnd = false;

	ALevelSequenceActor* OutActor = nullptr;
	ActiveSequencePlayer = ULevelSequencePlayer::CreateLevelSequencePlayer(World, Sequence, PlaybackSettings, OutActor);

	if (!ActiveSequencePlayer)
	{
		UE_LOG(LogTemp, Warning, TEXT("ABH_SpawnBeatTrigger '%s': failed to create sequence player for beat '%s'."), *GetName(), *BeatName);
		return;
	}

	ActiveSequencePlayer->OnFinished.AddDynamic(this, &ABH_SpawnBeatTrigger::HandleSequenceFinished);

	if (bDisablePlayerInputDuringBeat && InstigatingPawn)
	{
		SetPlayerInputLocked(InstigatingPawn, true);
	}

	UE_LOG(LogTemp, Log, TEXT("ABH_SpawnBeatTrigger '%s': playing spawn beat '%s'."), *GetName(), *BeatName);
	ActiveSequencePlayer->Play();
}

void ABH_SpawnBeatTrigger::SkipSpawnBeat()
{
	if (ActiveSequencePlayer && ActiveSequencePlayer->IsPlaying())
	{
		ActiveSequencePlayer->GoToEndAndStop();
	}
}

void ABH_SpawnBeatTrigger::HandleSequenceFinished()
{
	if (bDisablePlayerInputDuringBeat)
	{
		SetPlayerInputLocked(PawnUnderInputLock.Get(), false);
	}

	OnSpawnBeatFinished.Broadcast();
}

void ABH_SpawnBeatTrigger::SetPlayerInputLocked(APawn* Pawn, bool bLocked)
{
	if (!Pawn)
	{
		return;
	}

	APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
	if (!PC)
	{
		return;
	}

	if (bLocked)
	{
		PawnUnderInputLock = Pawn;
		PC->SetIgnoreMoveInput(true);
		PC->SetIgnoreLookInput(true);
	}
	else
	{
		PC->SetIgnoreMoveInput(false);
		PC->SetIgnoreLookInput(false);
		PawnUnderInputLock = nullptr;
	}
}
