// Blackwood Hollow - party combat / wipe state (implementation)

#include "Player/BH_PartyStateSubsystem.h"
#include "Player/BH_PlayerDeathComponent.h"
#include "Player/BH_GameState.h"
#include "Player/BH_PartyLibrary.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "Combat/BH_CombatIdentityComponent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "TimerManager.h"

bool UBH_PartyStateSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	// Server-authoritative: pure clients get the death state replicated on the pawns instead.
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->GetNetMode() != NM_Client;
}

void UBH_PartyStateSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	AggroSources.Reset();
	bInCombat = false;
	bWipeLatched = false;
	LastEngagedTime = -1.0e9;
	AggroHandle = UBH_CombatIdentityComponent::OnAnyAggroTargetChanged().AddUObject(this, &UBH_PartyStateSubsystem::HandleAggroTargetChanged);
}

void UBH_PartyStateSubsystem::Deinitialize()
{
	if (AggroHandle.IsValid())
	{
		UBH_CombatIdentityComponent::OnAnyAggroTargetChanged().Remove(AggroHandle);
		AggroHandle.Reset();
	}
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(EvaluateTimer);
	}
	AggroSources.Reset();
	Super::Deinitialize();
}

void UBH_PartyStateSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	InWorld.GetTimerManager().SetTimer(EvaluateTimer, this, &UBH_PartyStateSubsystem::Evaluate, FMath::Max(0.05f, EvaluateInterval), true);
}

UBH_PartyStateSubsystem* UBH_PartyStateSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UBH_PartyStateSubsystem>() : nullptr;
}

// ============================================================================
// Combat
// ============================================================================

void UBH_PartyStateSubsystem::HandleAggroTargetChanged(UBH_CombatIdentityComponent* Source, AActor* NewTarget)
{
	// The delegate is process-wide (every PIE world): only this world's enemies count.
	if (!Source || Source->GetWorld() != GetWorld())
	{
		return;
	}
	if (NewTarget)
	{
		AggroSources.Add(Source);
	}
	else
	{
		AggroSources.Remove(Source);
	}
}

void UBH_PartyStateSubsystem::Evaluate()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Engaged = a living enemy whose aggro target is a living player.
	bool bEngaged = false;
	for (auto It = AggroSources.CreateIterator(); It; ++It)
	{
		const UBH_CombatIdentityComponent* Source = It->Get();
		const AActor* Target = Source ? Source->GetAggroTarget() : nullptr;
		if (!Source || !Target)
		{
			It.RemoveCurrent();
			continue;
		}
		const AActor* Enemy = Source->GetOwner();
		const UAbilitySystemComponent* EnemyASC = Enemy ? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Enemy) : nullptr;
		const bool bEnemyAlive = Enemy && !Source->IsDead() && !(EnemyASC && EnemyASC->HasMatchingGameplayTag(TAG_State_Combat_Dead));
		if (bEnemyAlive && UBH_PlayerDeathComponent::IsLivingPlayer(Target))
		{
			bEngaged = true;
		}
	}

	const double Now = World->GetTimeSeconds();
	if (bEngaged)
	{
		LastEngagedTime = Now;
		if (!bInCombat)
		{
			bInCombat = true;
			PublishCombatState(true);
			UE_LOG(LogBHCombat, Log, TEXT("Party: in combat."));
			OnPartyCombatChanged.Broadcast(true);
		}
	}
	else if (bInCombat && Now - LastEngagedTime >= OutOfCombatGrace)
	{
		bInCombat = false;
		PublishCombatState(false);
		UE_LOG(LogBHCombat, Log, TEXT("Party: out of combat."));
		OnPartyCombatChanged.Broadcast(false);
		OpenReviveWindows();
	}

	EvaluateWipe();
}

// ============================================================================
// Players
// ============================================================================

void UBH_PartyStateSubsystem::ForEachPlayerComponent(TFunctionRef<void(UBH_PlayerDeathComponent&)> Fn) const
{
	// Party members only (UBH_PartyLibrary falls back to every player when there is no party game state).
	UBH_PartyLibrary::ForEachPartyMember(this, [&Fn](APlayerState&, APawn* MemberPawn)
	{
		if (UBH_PlayerDeathComponent* Comp = UBH_PlayerDeathComponent::Find(MemberPawn))
		{
			Fn(*Comp);
		}
	});
}

void UBH_PartyStateSubsystem::PublishCombatState(bool bNowInCombat) const
{
	// Mirror to the replicated game state so clients (death prompt) can read it.
	const UWorld* World = GetWorld();
	if (ABH_GameState* PartyGameState = World ? World->GetGameState<ABH_GameState>() : nullptr)
	{
		PartyGameState->SetPartyInCombat(bNowInCombat);
	}
}

bool UBH_PartyStateSubsystem::HasOtherLivingPlayer(const UBH_PlayerDeathComponent* Excluding) const
{
	bool bFound = false;
	ForEachPlayerComponent([&bFound, Excluding](UBH_PlayerDeathComponent& Comp)
	{
		if (&Comp != Excluding && UBH_PlayerDeathComponent::IsLivingPlayer(Comp.GetOwner()))
		{
			bFound = true;
		}
	});
	return bFound;
}

void UBH_PartyStateSubsystem::OpenReviveWindows()
{
	ForEachPlayerComponent([](UBH_PlayerDeathComponent& Comp)
	{
		Comp.TryOpenReviveWindow();
	});
}

void UBH_PartyStateSubsystem::NotifyPlayerStateChanged()
{
	EvaluateWipe();
	// A death can leave another dead player alone (nobody left to wait for): let the windows re-check.
	OpenReviveWindows();
}

void UBH_PartyStateSubsystem::EvaluateWipe()
{
	int32 NumPlayers = 0;
	int32 NumLiving = 0;
	ForEachPlayerComponent([&NumPlayers, &NumLiving](UBH_PlayerDeathComponent& Comp)
	{
		++NumPlayers;
		if (UBH_PlayerDeathComponent::IsLivingPlayer(Comp.GetOwner()))
		{
			++NumLiving;
		}
	});

	if (NumPlayers > 0 && NumLiving == 0)
	{
		if (!bWipeLatched)
		{
			bWipeLatched = true;
			UE_LOG(LogBHCombat, Log, TEXT("Party wiped (%d player(s) down)."), NumPlayers);
			// TODO(GDD 5.5): enemies reset, encounters do NOT. Not implemented yet: listeners bind to OnPartyWiped.
			OnPartyWiped.Broadcast();
		}
	}
	else if (NumLiving > 0)
	{
		bWipeLatched = false; // somebody is up again: the next wipe fires again
	}
}
