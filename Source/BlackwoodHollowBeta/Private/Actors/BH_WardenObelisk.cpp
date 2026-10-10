// Blackwood Hollow - the Warden obelisk (implementation)

#include "Actors/BH_WardenObelisk.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Player/BH_PlayerState.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogBHWardenObelisk, Log, All);

ABH_WardenObelisk::ABH_WardenObelisk()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false; // the learned flag lives on each PlayerState; the obelisk itself has no replicated state

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	SetRootComponent(BodyMesh);
	BodyMesh->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	BodyMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

	Interactable = CreateDefaultSubobject<UBH_InteractableComponent>(TEXT("Interactable"));
	Interactable->PromptName = NSLOCTEXT("BlackwoodHollow", "WardenObeliskName", "Warden Obelisk");
	Interactable->PromptAction = NSLOCTEXT("BlackwoodHollow", "WardenObeliskAction", "Touch");
	Interactable->HoldSeconds = 0.f;
	Interactable->InteractRange = 250.f;
}

void ABH_WardenObelisk::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() != NM_DedicatedServer)
	{
		GetWorldTimerManager().SetTimer(PendingTimer, this, &ABH_WardenObelisk::PollPending, FMath::Max(0.1f, PendingPollInterval), true);
	}
}

void ABH_WardenObelisk::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(PendingTimer);
	Super::EndPlay(EndPlayReason);
}

int32 ABH_WardenObelisk::GetNumPlayersWithoutBurst() const
{
	const UWorld* World = GetWorld();
	const AGameStateBase* GameStateBase = World ? World->GetGameState() : nullptr;
	if (!GameStateBase)
	{
		return 0;
	}
	int32 Count = 0;
	for (const APlayerState* PlayerState : GameStateBase->PlayerArray)
	{
		const ABH_PlayerState* BHState = Cast<ABH_PlayerState>(PlayerState);
		if (BHState && !BHState->IsInactive() && !BHState->HasLearnedOverloadBurst())
		{
			++Count;
		}
	}
	return Count;
}

bool ABH_WardenObelisk::HasLocalPlayerLearned() const
{
	const UWorld* World = GetWorld();
	const APlayerController* LocalPC = World ? World->GetFirstPlayerController() : nullptr;
	const ABH_PlayerState* LocalState = LocalPC ? LocalPC->GetPlayerState<ABH_PlayerState>() : nullptr;
	return LocalState && LocalState->HasLearnedOverloadBurst();
}

bool ABH_WardenObelisk::IsPartnerPending() const
{
	return HasLocalPlayerLearned() && GetNumPlayersWithoutBurst() > 0;
}

bool ABH_WardenObelisk::BH_CanInteract(const APawn* InteractingPawn, FText& OutDenyReason) const
{
	OutDenyReason = FText::GetEmpty();
	const ABH_PlayerState* BHState = InteractingPawn ? InteractingPawn->GetPlayerState<ABH_PlayerState>() : nullptr;
	// Already learned: not offered at all (empty reason hides the prompt).
	return BHState && !BHState->HasLearnedOverloadBurst();
}

void ABH_WardenObelisk::BH_OnInteractionCompleted(APawn* InteractingPawn)
{
	if (GetNetMode() == NM_Client || !InteractingPawn)
	{
		return;
	}
	ABH_PlayerState* BHState = InteractingPawn->GetPlayerState<ABH_PlayerState>();
	if (!BHState || BHState->HasLearnedOverloadBurst())
	{
		return;
	}
	const bool bEquipped = BHState->GrantOverloadBurst();
	UE_LOG(LogBHWardenObelisk, Log, TEXT("%s: %s learned Overload Burst (equipped: %s)."), *GetNameSafe(this), *GetNameSafe(BHState), bEquipped ? TEXT("yes") : TEXT("no"));
	PollPending(); // refresh the marker on the server / host right away
}

void ABH_WardenObelisk::PollPending()
{
	const int32 Count = GetNumPlayersWithoutBurst();
	if (Count != LastPendingCount)
	{
		LastPendingCount = Count;
		BP_OnPendingChanged(Count);
	}
}
