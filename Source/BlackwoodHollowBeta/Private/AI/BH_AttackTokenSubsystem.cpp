// Blackwood Hollow - attack tokens (implementation)

#include "AI/BH_AttackTokenSubsystem.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ConfigCacheIni.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"

DEFINE_LOG_CATEGORY(LogBHTokens);

bool UBH_AttackTokenSubsystem::bEnableAttackTokens = true;

static TAutoConsoleVariable<int32> CVarBHAttackTokensDebug(
	TEXT("bh.AttackTokens.Debug"),
	0,
	TEXT("1 = draw a debug string above attack-token holders (TOKEN) and waiting AIs (wait seconds)."),
	ECVF_Default);

namespace
{
	/** The controller's pawn view direction on the XY plane (control rotation for a player, actor forward for an AI). */
	FVector TargetViewDirection2D(const AActor* Target)
	{
		if (const APawn* Pawn = Cast<APawn>(Target))
		{
			return FRotator(0.f, Pawn->GetViewRotation().Yaw, 0.f).Vector();
		}
		return Target ? Target->GetActorForwardVector().GetSafeNormal2D() : FVector::ForwardVector;
	}
}

// ============================================================================
// Subsystem plumbing
// ============================================================================

bool UBH_AttackTokenSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	// AI controllers (the only requesters) exist on the server only.
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->GetNetMode() != NM_Client;
}

void UBH_AttackTokenSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	bool bEnabled = true;
	if (GConfig && GConfig->GetBool(TEXT("/Script/BlackwoodHollowBeta.BH_AttackTokenSubsystem"), TEXT("bEnableAttackTokens"), bEnabled, GGameIni))
	{
		bEnableAttackTokens = bEnabled;
	}
	Targets.Reset();
	UE_LOG(LogBHTokens, Log, TEXT("Attack tokens %s: max=%d handoff=%.2fs maxHold=%.1fs"),
		bEnableAttackTokens ? TEXT("enabled") : TEXT("DISABLED"), MaxTokensPerTarget, HandoffCooldown, MaxHoldTime);
}

TStatId UBH_AttackTokenSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UBH_AttackTokenSubsystem, STATGROUP_Tickables);
}

UBH_AttackTokenSubsystem* UBH_AttackTokenSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UBH_AttackTokenSubsystem>() : nullptr;
}

double UBH_AttackTokenSubsystem::Now() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

// ============================================================================
// Scoring / validity
// ============================================================================

float UBH_AttackTokenSubsystem::ScoreWaiter(const FWaiter& Waiter, const AActor* Target, double Time) const
{
	float Score = static_cast<float>(Time - Waiter.FirstRequest); // time waited: the dominant term

	const AController* Controller = Waiter.Controller.Get();
	const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (Pawn && Target)
	{
		const FVector ToRequester = Pawn->GetActorLocation() - Target->GetActorLocation();
		const FVector Dir2D = ToRequester.GetSafeNormal2D();
		if (!Dir2D.IsNearlyZero())
		{
			const float CosAngle = static_cast<float>(FVector::DotProduct(Dir2D, TargetViewDirection2D(Target)));
			if (CosAngle >= FMath::Cos(FMath::DegreesToRadians(FrontHalfAngleDeg)))
			{
				Score += FrontBonusSeconds;
			}
		}
		Score -= DistancePenaltyPerMeter * static_cast<float>(ToRequester.Size()) * 0.01f;
	}
	return Score;
}

bool UBH_AttackTokenSubsystem::ShouldAutoRelease(const FHolder& Holder, const AActor* Target, double Time) const
{
	const AController* Controller = Holder.Controller.Get();
	if (!Controller || !IsValid(Target) || Time - Holder.GrantTime > MaxHoldTime)
	{
		return true;
	}
	const APawn* Pawn = Controller->GetPawn();
	if (!IsValid(Pawn))
	{
		return true;
	}
	if (const UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(const_cast<APawn*>(Pawn)))
	{
		return ASC->HasMatchingGameplayTag(TAG_State_Combat_Dead)
			|| ASC->HasMatchingGameplayTag(TAG_State_Combat_PostureBroken)
			|| ASC->HasMatchingGameplayTag(TAG_State_Combat_Staggered);
	}
	return false;
}

void UBH_AttackTokenSubsystem::PruneWaiters(FTargetTokens& Entry, double Time) const
{
	Entry.Waiters.RemoveAll([this, Time](const FWaiter& Waiter)
	{
		return !Waiter.Controller.IsValid() || Time - Waiter.LastRequest > WaiterStaleTime || Time < Waiter.LastRequest;
	});
}

// ============================================================================
// API
// ============================================================================

bool UBH_AttackTokenSubsystem::RequestToken(AActor* Target, AController* Requester)
{
	if (!bEnableAttackTokens)
	{
		return true;
	}
	if (!IsValid(Target) || !IsValid(Requester))
	{
		return false;
	}

	const double Time = Now();
	FTargetTokens& Entry = Targets.FindOrAdd(Target);

	// Already a holder: idempotent.
	for (const FHolder& Holder : Entry.Holders)
	{
		if (Holder.Controller.Get() == Requester)
		{
			return true;
		}
	}

	// Register / refresh as a waiter (a requester that went stale starts its wait over).
	PruneWaiters(Entry, Time);
	FWaiter* Waiter = Entry.Waiters.FindByPredicate([Requester](const FWaiter& W) { return W.Controller.Get() == Requester; });
	if (!Waiter)
	{
		FWaiter NewWaiter;
		NewWaiter.Controller = Requester;
		NewWaiter.FirstRequest = Time;
		NewWaiter.LastRequest = Time;
		Waiter = &Entry.Waiters.Add_GetRef(NewWaiter);
	}
	Waiter->LastRequest = Time;

	if (Entry.Holders.Num() >= MaxTokensPerTarget || Time - Entry.LastReleaseTime < HandoffCooldown)
	{
		return false;
	}

	// A token is free: only the best-scored fresh waiter may take it.
	const FWaiter* Best = nullptr;
	float BestScore = -TNumericLimits<float>::Max();
	for (const FWaiter& Candidate : Entry.Waiters)
	{
		const float Score = ScoreWaiter(Candidate, Target, Time);
		const bool bBetter = !Best || Score > BestScore
			|| (FMath::IsNearlyEqual(Score, BestScore) && (Candidate.FirstRequest < Best->FirstRequest
				|| (Candidate.FirstRequest == Best->FirstRequest && Candidate.Controller.Get()->GetUniqueID() < Best->Controller.Get()->GetUniqueID())));
		if (bBetter)
		{
			Best = &Candidate;
			BestScore = Score;
		}
	}
	if (!Best || Best->Controller.Get() != Requester)
	{
		return false;
	}

	FHolder NewHolder;
	NewHolder.Controller = Requester;
	NewHolder.GrantTime = Time;
	Entry.Holders.Add(NewHolder);
	++StatGrants;
	StatMaxHoldersObserved = FMath::Max(StatMaxHoldersObserved, Entry.Holders.Num());
	Entry.Waiters.RemoveAll([Requester](const FWaiter& W) { return W.Controller.Get() == Requester; });
	UE_LOG(LogBHTokens, Verbose, TEXT("Token granted: target=%s holder=%s score=%.2f (holders=%d, waiting=%d)"),
		*GetNameSafe(Target), *GetNameSafe(Requester), BestScore, Entry.Holders.Num(), Entry.Waiters.Num());
	return true;
}

void UBH_AttackTokenSubsystem::ReleaseToken(AActor* Target, AController* Holder)
{
	if (!Target || !Holder)
	{
		return;
	}
	FTargetTokens* Entry = Targets.Find(Target);
	if (!Entry)
	{
		return;
	}
	Entry->Waiters.RemoveAll([Holder](const FWaiter& W) { return W.Controller.Get() == Holder; });
	const int32 Removed = Entry->Holders.RemoveAll([Holder](const FHolder& H) { return H.Controller.Get() == Holder; });
	if (Removed > 0)
	{
		Entry->LastReleaseTime = Now();
		UE_LOG(LogBHTokens, Verbose, TEXT("Token released: target=%s holder=%s"), *GetNameSafe(Target), *GetNameSafe(Holder));
	}
}

bool UBH_AttackTokenSubsystem::HasToken(const AActor* Target, const AController* Holder) const
{
	if (!bEnableAttackTokens)
	{
		return true;
	}
	const FTargetTokens* Entry = Target ? Targets.Find(Target) : nullptr;
	return Entry && Entry->Holders.ContainsByPredicate([Holder](const FHolder& H) { return H.Controller.Get() == Holder; });
}

AActor* UBH_AttackTokenSubsystem::GetHolder(const AActor* Target) const
{
	const FTargetTokens* Entry = Target ? Targets.Find(Target) : nullptr;
	if (Entry)
	{
		for (const FHolder& H : Entry->Holders)
		{
			if (const AController* Controller = H.Controller.Get())
			{
				return Controller->GetPawn();
			}
		}
	}
	return nullptr;
}

int32 UBH_AttackTokenSubsystem::GetHolderCount(const AActor* Target) const
{
	const FTargetTokens* Entry = Target ? Targets.Find(Target) : nullptr;
	return Entry ? Entry->Holders.Num() : 0;
}

int32 UBH_AttackTokenSubsystem::GetWaiterCount(const AActor* Target) const
{
	const FTargetTokens* Entry = Target ? Targets.Find(Target) : nullptr;
	return Entry ? Entry->Waiters.Num() : 0;
}

// ============================================================================
// Tick (0.1 s): auto release, housekeeping, debug
// ============================================================================

void UBH_AttackTokenSubsystem::Tick(float DeltaTime)
{
	TickAccumulator += DeltaTime;
	if (TickAccumulator < 0.1f)
	{
		return;
	}
	TickAccumulator = 0.f;

	const double Time = Now();
	for (auto It = Targets.CreateIterator(); It; ++It)
	{
		AActor* Target = It.Key().Get();
		FTargetTokens& Entry = It.Value();

		for (int32 Index = Entry.Holders.Num() - 1; Index >= 0; --Index)
		{
			if (ShouldAutoRelease(Entry.Holders[Index], Target, Time))
			{
				UE_LOG(LogBHTokens, Verbose, TEXT("Token auto-released: target=%s holder=%s"), *GetNameSafe(Target), *GetNameSafe(Entry.Holders[Index].Controller.Get()));
				Entry.Holders.RemoveAt(Index);
				Entry.LastReleaseTime = Time;
			}
		}
		PruneWaiters(Entry, Time);

		if (!Target || (Entry.Holders.Num() == 0 && Entry.Waiters.Num() == 0 && Time - Entry.LastReleaseTime > HandoffCooldown))
		{
			It.RemoveCurrent();
		}
	}

	DrawDebug(Time);
}

void UBH_AttackTokenSubsystem::DrawDebug(double Time) const
{
#if ENABLE_DRAW_DEBUG
	if (CVarBHAttackTokensDebug.GetValueOnGameThread() <= 0)
	{
		return;
	}
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	// On-screen summary (GEngine messages show in every viewport of this process, including a PIE client window).
	if (GEngine)
	{
		int32 Line = 0;
		for (const TPair<TWeakObjectPtr<AActor>, FTargetTokens>& Pair : Targets)
		{
			FString Msg = FString::Printf(TEXT("[Tokens] %s  holder: "), *GetNameSafe(Pair.Key.Get()));
			if (Pair.Value.Holders.Num() == 0)
			{
				Msg += TEXT("none");
			}
			for (const FHolder& Holder : Pair.Value.Holders)
			{
				const APawn* Pawn = Holder.Controller.IsValid() ? Holder.Controller->GetPawn() : nullptr;
				Msg += FString::Printf(TEXT("%s (%.1fs) "), *GetNameSafe(Pawn), Time - Holder.GrantTime);
			}
			Msg += FString::Printf(TEXT(" | waiting: %d"), Pair.Value.Waiters.Num());
			for (const FWaiter& Waiter : Pair.Value.Waiters)
			{
				const APawn* Pawn = Waiter.Controller.IsValid() ? Waiter.Controller->GetPawn() : nullptr;
				Msg += FString::Printf(TEXT("  %s %.1fs"), *GetNameSafe(Pawn), Time - Waiter.FirstRequest);
			}
			// Stable key per line so the message updates in place instead of scrolling.
			GEngine->AddOnScreenDebugMessage(static_cast<uint64>(0xB4A770C0 + Line++), 0.15f, FColor::Orange, Msg);
		}
		GEngine->AddOnScreenDebugMessage(static_cast<uint64>(0xB4A770BF), 0.15f, FColor::Orange,
			FString::Printf(TEXT("[Tokens] grants=%d  max simultaneous holders on one target=%d (limit %d)"), StatGrants, StatMaxHoldersObserved, MaxTokensPerTarget));
	}
	for (const TPair<TWeakObjectPtr<AActor>, FTargetTokens>& Pair : Targets)
	{
		for (const FHolder& Holder : Pair.Value.Holders)
		{
			if (const APawn* Pawn = Holder.Controller.IsValid() ? Holder.Controller->GetPawn() : nullptr)
			{
				DrawDebugString(World, Pawn->GetActorLocation() + FVector(0.0, 0.0, 130.0),
					FString::Printf(TEXT("TOKEN %.1fs"), Time - Holder.GrantTime), nullptr, FColor::Red, 0.12f, true);
			}
		}
		for (const FWaiter& Waiter : Pair.Value.Waiters)
		{
			if (const APawn* Pawn = Waiter.Controller.IsValid() ? Waiter.Controller->GetPawn() : nullptr)
			{
				DrawDebugString(World, Pawn->GetActorLocation() + FVector(0.0, 0.0, 130.0),
					FString::Printf(TEXT("wait %.1fs"), Time - Waiter.FirstRequest), nullptr, FColor::Yellow, 0.12f, true);
			}
		}
	}
#endif
}
