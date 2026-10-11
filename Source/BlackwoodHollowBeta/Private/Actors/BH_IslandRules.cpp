// Blackwood Hollow - per-island rules actor (implementation)

#include "Actors/BH_IslandRules.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "Combat/BH_LoadoutComponent.h"
#include "Items/BH_EquipmentTypes.h"
#include "Player/BH_PlayerState.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "TimerManager.h"

ABH_IslandRules::ABH_IslandRules()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false; // level data: identical on every machine
	SetCanBeDamaged(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
}

ABH_IslandRules* ABH_IslandRules::FindRules(const UObject* WorldContext)
{
	UWorld* ContextWorld = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!ContextWorld)
	{
		return nullptr;
	}
	TActorIterator<ABH_IslandRules> It(ContextWorld);
	return It ? *It : nullptr;
}

bool ABH_IslandRules::IsWeaponInputLocked(const AActor* PlayerPawn)
{
	const ABH_IslandRules* Rules = FindRules(PlayerPawn);
	if (!Rules || !Rules->bStartUnarmed)
	{
		return false;
	}
	const UBH_LoadoutComponent* Loadout = UBH_LoadoutComponent::FindLoadoutComponent(PlayerPawn);
	if (!Loadout)
	{
		return false; // not a player pawn
	}
	return Loadout->GetStanceForSet(EBH_LoadoutSet::A).IsNone() && Loadout->GetStanceForSet(EBH_LoadoutSet::B).IsNone();
}

void ABH_IslandRules::BeginPlay()
{
	Super::BeginPlay();

	// The actor is not replicated, so a client's copy also reports HasAuthority(): ask the net mode instead. Only the server applies the rules.
	if (GetNetMode() == NM_Client)
	{
		return;
	}
	ApplyToNewPlayers();
	GetWorldTimerManager().SetTimer(ApplyTimer, this, &ABH_IslandRules::ApplyToNewPlayers, FMath::Max(ApplyInterval, 0.05f), true);
}

void ABH_IslandRules::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(ApplyTimer);
	Super::EndPlay(EndPlayReason);
}

void ABH_IslandRules::ApplyToNewPlayers()
{
	const AGameStateBase* State = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (!State)
	{
		return;
	}
	for (APlayerState* PlayerStateEntry : State->PlayerArray)
	{
		ABH_PlayerState* BHState = Cast<ABH_PlayerState>(PlayerStateEntry);
		if (!BHState)
		{
			continue;
		}
		const TWeakObjectPtr<APlayerState> HandledKey(PlayerStateEntry);
		if (HandledStates.Contains(HandledKey))
		{
			continue;
		}
		HandledStates.Add(HandledKey);
		ApplyToPlayerState(BHState);
	}
}

void ABH_IslandRules::ApplyToPlayerState(ABH_PlayerState* TargetState)
{
	if (!TargetState)
	{
		return;
	}
	if (bLockWeaponSetB)
	{
		TargetState->SetWeaponSetBUnlocked(false);
	}
	if (bStartWithoutFragmentSkills)
	{
		for (int32 SlotIndex = 0; SlotIndex < ABH_PlayerState::NumFragmentSlots; ++SlotIndex)
		{
			TargetState->UnequipFragment(SlotIndex);
		}
	}
	UE_LOG(LogBHCombat, Log, TEXT("Island rules applied to %s (unarmed start: %s, set B locked: %s, no fragment skills: %s)."), *TargetState->GetPlayerName(),
		bStartUnarmed ? TEXT("yes") : TEXT("no"), bLockWeaponSetB ? TEXT("yes") : TEXT("no"), bStartWithoutFragmentSkills ? TEXT("yes") : TEXT("no"));
}
