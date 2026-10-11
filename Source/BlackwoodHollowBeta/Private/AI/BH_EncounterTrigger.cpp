// Blackwood Hollow - Phase 12G encounter trigger (implementation)

#include "AI/BH_EncounterTrigger.h"
#include "AI/BH_EnemyWaveSpawner.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "Components/BoxComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"

ABH_EncounterTrigger::ABH_EncounterTrigger()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;

	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	SetRootComponent(Box);
	Box->SetBoxExtent(BoxExtent);
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetCollisionObjectType(ECC_WorldDynamic);
	Box->SetCollisionResponseToAllChannels(ECR_Ignore);
	Box->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Box->SetGenerateOverlapEvents(true);
	Box->SetHiddenInGame(true);
}

void ABH_EncounterTrigger::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Box->SetBoxExtent(BoxExtent);
}

void ABH_EncounterTrigger::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() == NM_Client)
	{
		return; // server-only logic: the spawners it starts exist and run on the server
	}
	Box->OnComponentBeginOverlap.AddDynamic(this, &ABH_EncounterTrigger::HandleBeginOverlap);

	// A player already standing inside at BeginPlay does not generate an overlap event.
	TArray<AActor*> Inside;
	Box->GetOverlappingActors(Inside, APawn::StaticClass());
	for (AActor* Actor : Inside)
	{
		const APawn* Pawn = Cast<APawn>(Actor);
		if (Pawn && Pawn->IsPlayerControlled())
		{
			HandleBeginOverlap(Box, Actor, nullptr, 0, false, FHitResult());
			break;
		}
	}
}

void ABH_EncounterTrigger::HandleBeginOverlap(UPrimitiveComponent* /*OverlappedComponent*/, AActor* OtherActor, UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/, bool /*bFromSweep*/, const FHitResult& /*SweepResult*/)
{
	if (bFired || GetNetMode() == NM_Client)
	{
		return;
	}
	const APawn* Pawn = Cast<APawn>(OtherActor);
	if (!Pawn || !Pawn->IsPlayerControlled())
	{
		return;
	}
	bFired = true;
	int32 Started = 0;
	for (ABH_EnemyWaveSpawner* Spawner : Spawners)
	{
		if (Spawner)
		{
			Spawner->StartWaves();
			++Started;
		}
	}
	UE_LOG(LogBHCombat, Log, TEXT("EncounterTrigger '%s': %s entered, %d spawner(s) started."), *GetName(), *GetNameSafe(OtherActor), Started);
}
