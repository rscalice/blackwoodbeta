
// Blackwood Hollow - HitFeedback (implementation)

#include "Combat/BH_HitFeedback.h"
#include "Combat/BH_CombatIdentityComponent.h"
#include "Characters/BH_EnemyBase.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

namespace
{
	bool IsLocallyControlledPawn(const AActor* Actor)
	{
		const APawn* Pawn = Cast<APawn>(Actor);
		return Pawn && Pawn->IsLocallyControlled();
	}
}

UBH_HitFeedbackSubsystem* UBH_HitFeedbackSubsystem::GetHitFeedbackSubsystem(const UObject* WorldContext)
{
	const UWorld* ContextWorld = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	return ContextWorld ? ContextWorld->GetSubsystem<UBH_HitFeedbackSubsystem>() : nullptr;
}

void UBH_HitFeedbackSubsystem::Report(const UObject* WorldContext, AActor* Attacker, AActor* Victim, EBH_ImpactTier Tier, EBH_HitOutcome Outcome,
	bool bFatal, const FVector& Location, const FVector& Direction)
{
	const UWorld* ContextWorld = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!ContextWorld || ContextWorld->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	UBH_HitFeedbackSubsystem* Hub = ContextWorld->GetSubsystem<UBH_HitFeedbackSubsystem>();
	if (!Hub)
	{
		return;
	}

	FBH_HitFeedback Feedback;
	Feedback.Attacker = Attacker;
	Feedback.Victim = Victim;
	Feedback.Tier = Tier;
	Feedback.Surface = UBH_HitFeedbackLibrary::GetHitSurface(Victim);
	Feedback.bIsBlighted = UBH_HitFeedbackLibrary::IsActorBlighted(Victim);
	Feedback.Outcome = Outcome;
	Feedback.bFatal = bFatal;
	Feedback.Location = Location;
	Feedback.Direction = Direction;
	Feedback.bAttackerLocallyControlled = IsLocallyControlledPawn(Attacker);
	Feedback.bVictimLocallyControlled = IsLocallyControlledPawn(Victim);
	Hub->OnHitFeedback.Broadcast(Feedback);
}

EBH_HitSurface UBH_HitFeedbackLibrary::GetHitSurface(const AActor* Actor)
{
	if (const UBH_CombatIdentityComponent* Identity = UBH_CombatIdentityComponent::Find(Actor))
	{
		return Identity->SurfaceType;
	}
	if (const ABH_EnemyBase* Enemy = Cast<ABH_EnemyBase>(Actor))
	{
		return Enemy->SurfaceType;
	}
	return EBH_HitSurface::Flesh;
}

bool UBH_HitFeedbackLibrary::IsActorBlighted(const AActor* Actor)
{
	if (const UBH_CombatIdentityComponent* Identity = UBH_CombatIdentityComponent::Find(Actor))
	{
		return Identity->bBlighted;
	}
	if (const ABH_EnemyBase* Enemy = Cast<ABH_EnemyBase>(Actor))
	{
		return Enemy->bBlighted;
	}
	return false;
}
