// Blackwood Hollow - arena entrance trigger (implementation)

#include "Actors/BH_ArenaTrigger.h"
#include "AI/BH_EnemyWaveSpawner.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "Components/BoxComponent.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "TimerManager.h"

ABH_ArenaTrigger::ABH_ArenaTrigger()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false; // server-side logic only

	TriggerBox = CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerBox"));
	SetRootComponent(TriggerBox);
	TriggerBox->SetBoxExtent(FVector(200.f, 250.f, 200.f));
	TriggerBox->SetCollisionProfileName(TEXT("Trigger"));
	TriggerBox->SetGenerateOverlapEvents(true);
	TriggerBox->ShapeColor = FColor(200, 60, 40);
	TriggerBox->SetHiddenInGame(true);
}

void ABH_ArenaTrigger::BeginPlay()
{
	Super::BeginPlay();

	// Bound at runtime (not in the constructor) so Blueprint children that replace the box keep working.
	if (HasAuthority() && TriggerBox)
	{
		TriggerBox->OnComponentBeginOverlap.AddDynamic(this, &ABH_ArenaTrigger::HandleBeginOverlap);
	}
}

void ABH_ArenaTrigger::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(StartDelayTimer);
	}
	Super::EndPlay(EndPlayReason);
}

void ABH_ArenaTrigger::HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!HasAuthority())
	{
		return;
	}
	APawn* Pawn = Cast<APawn>(OtherActor);
	if (!Pawn)
	{
		return;
	}
	if (bRequirePlayerControlled && !Pawn->IsPlayerControlled())
	{
		return;
	}
	if (bTriggered && bTriggerOnce)
	{
		return;
	}

	bTriggered = true;
	if (bTriggerOnce && TriggerBox)
	{
		TriggerBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	UE_LOG(LogBHCombat, Log, TEXT("ArenaTrigger '%s': tripped by %s (delay %.2fs)"), *GetName(), *GetNameSafe(Pawn), StartDelay);

	if (StartDelay > 0.f)
	{
		GetWorldTimerManager().SetTimer(StartDelayTimer, FTimerDelegate::CreateWeakLambda(this, [this, WeakPawn = TWeakObjectPtr<APawn>(Pawn)]()
		{
			FireTrigger(WeakPawn.Get());
		}), StartDelay, false);
	}
	else
	{
		FireTrigger(Pawn);
	}
}

void ABH_ArenaTrigger::FireTrigger(APawn* TriggeringPawn)
{
	for (ABH_EnemyWaveSpawner* Spawner : Spawners)
	{
		if (!IsValid(Spawner))
		{
			continue;
		}
		if (Spawner->IsRunning())
		{
			UE_LOG(LogBHCombat, Verbose, TEXT("ArenaTrigger '%s': spawner '%s' already running, skipped"), *GetName(), *Spawner->GetName());
			continue;
		}
		Spawner->StartWaves();
	}

	OnArenaTriggered.Broadcast(TriggeringPawn);
	K2_OnArenaTriggered(TriggeringPawn);
}

void ABH_ArenaTrigger::ResetTrigger()
{
	if (!HasAuthority())
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(StartDelayTimer);
	bTriggered = false;
	if (TriggerBox)
	{
		TriggerBox->SetCollisionProfileName(TEXT("Trigger"));
		TriggerBox->SetGenerateOverlapEvents(true);
	}
}
