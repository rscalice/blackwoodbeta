// Blackwood Hollow - beat trigger volume (Phase 13B+, implementation)

#include "Beats/BH_BeatTrigger.h"
#include "Beats/BH_BeatTrackerComponent.h"
#include "Components/BoxComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Quests/BH_QuestDirector.h"
#include "Quests/BH_QuestEvents.h"

ABH_BeatTrigger::ABH_BeatTrigger()
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

void ABH_BeatTrigger::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		Box->OnComponentBeginOverlap.AddDynamic(this, &ABH_BeatTrigger::HandleBeginOverlap);
	}
}

void ABH_BeatTrigger::HandleBeginOverlap(UPrimitiveComponent* /*OverlappedComponent*/, AActor* OtherActor, UPrimitiveComponent* /*OtherComp*/, int32 /*OtherBodyIndex*/, bool /*bFromSweep*/, const FHitResult& /*SweepResult*/)
{
	const APawn* Pawn = Cast<APawn>(OtherActor);
	if (!Pawn || (bRequirePlayerControlled && !Pawn->IsPlayerControlled()))
	{
		return;
	}
	UBH_BeatTrackerComponent::CompleteBeatOnServer(this, BeatToComplete);

	// 14A-1: EnterArea(<QuestAreaId>), once per player per volume (re-entering must not inflate a counted task).
	if (!QuestAreaId.IsNone())
	{
		APlayerState* VisitorState = Pawn->GetPlayerState();
		const TWeakObjectPtr<APlayerState> VisitorKey(VisitorState);
		if (VisitorState && !AreaReportedBy.Contains(VisitorKey))
		{
			AreaReportedBy.Add(VisitorKey);
			UBH_QuestDirector::ReportQuestEvent(this, FName(BH_QuestEvents::EnterArea), QuestAreaId.ToString());
		}
	}
}