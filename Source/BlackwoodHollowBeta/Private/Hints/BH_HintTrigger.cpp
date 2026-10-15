// Blackwood Hollow - hint trigger volume (Phase 13E, implementation)

#include "Hints/BH_HintTrigger.h"
#include "Hints/BH_HintData.h"
#include "Hints/BH_HintLibrary.h"
#include "Components/BoxComponent.h"
#include "GameFramework/Pawn.h"

ABH_HintTrigger::ABH_HintTrigger()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;

	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	Box->SetBoxExtent(FVector(300.0, 300.0, 200.0));
	Box->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	Box->SetGenerateOverlapEvents(true);
	Box->SetCanEverAffectNavigation(false);
	SetRootComponent(Box);
}

void ABH_HintTrigger::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		Box->OnComponentBeginOverlap.AddDynamic(this, &ABH_HintTrigger::HandleBeginOverlap);
	}
}

void ABH_HintTrigger::HandleBeginOverlap(UPrimitiveComponent* /*OverlappedComponent*/, AActor* OtherActor, UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/, bool /*bFromSweep*/, const FHitResult& /*SweepResult*/)
{
	const APawn* Pawn = Cast<APawn>(OtherActor);
	if (!Hint || !Pawn || (bRequirePlayerControlled && !Pawn->IsPlayerControlled()))
	{
		return;
	}
	UBH_HintLibrary::TriggerHint(Pawn, Hint->HintId);
}
