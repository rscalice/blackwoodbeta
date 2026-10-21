
// Blackwood Hollow - HitFeedback (implementation)

#include "Combat/BH_HitFeedback.h"
#include "Combat/BH_CombatIdentityComponent.h"
#include "Characters/BH_EnemyBase.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

#if !UE_BUILD_SHIPPING
DEFINE_LOG_CATEGORY_STATIC(LogBHHitFeedback, Log, All);

static TAutoConsoleVariable<int32> CVarBHHitFeedbackDebug(
	TEXT("bh.HitFeedback.Debug"),
	0,
	TEXT("1 = at every OnHitFeedback broadcast on this machine, log a line (LogBHHitFeedback) and show an on-screen message for 2 s: net mode, roles, attacker, victim, tier, surface, outcome. For testing. Default 0."),
	ECVF_Default);
#endif

namespace
{
	bool IsLocallyControlledPawn(const AActor* Actor)
	{
		const APawn* Pawn = Cast<APawn>(Actor);
		return Pawn && Pawn->IsLocallyControlled();
	}

#if !UE_BUILD_SHIPPING
	template <typename EnumType>
	FString EnumName(EnumType Value)
	{
		const UEnum* Enum = StaticEnum<EnumType>();
		return Enum ? Enum->GetNameStringByValue(static_cast<int64>(Value)) : FString(TEXT("?"));
	}

	const TCHAR* NetModeName(const ENetMode Mode)
	{
		switch (Mode)
		{
		case NM_Standalone: return TEXT("Standalone");
		case NM_DedicatedServer: return TEXT("DedicatedServer");
		case NM_ListenServer: return TEXT("ListenServer");
		case NM_Client: return TEXT("Client");
		default: return TEXT("?");
		}
	}

	FString ActorDesc(const AActor* Actor, const bool bLocal)
	{
		if (!Actor)
		{
			return TEXT("none");
		}
		return FString::Printf(TEXT("%s [%s%s]"), *Actor->GetName(), *EnumName(Actor->GetLocalRole()), bLocal ? TEXT(", local") : TEXT(""));
	}

	/** bh.HitFeedback.Debug 1: one log line and one on-screen message per broadcast. */
	void DebugReport(const UWorld* World, const FBH_HitFeedback& Feedback)
	{
		const FString Line = FString::Printf(TEXT("HitFeedback [%s | %s] atk=%s vic=%s tier=%s surface=%s outcome=%s fatal=%d blight=%d"),
			NetModeName(World->GetNetMode()), *World->GetName(),
			*ActorDesc(Feedback.Attacker, Feedback.bAttackerLocallyControlled), *ActorDesc(Feedback.Victim, Feedback.bVictimLocallyControlled),
			*EnumName(Feedback.Tier), *EnumName(Feedback.Surface), *EnumName(Feedback.Outcome),
			Feedback.bFatal ? 1 : 0, Feedback.bIsBlighted ? 1 : 0);
		UE_LOG(LogBHHitFeedback, Log, TEXT("%s"), *Line);

		if (GEngine)
		{
			// A small ring of fixed keys: each message lives 2 s on its own row, and rapid hits stack instead of overwriting each other.
			static uint32 Counter = 0;
			constexpr uint64 KeyBase = 0x48464230ull; // "HFB0"
			const uint64 Key = KeyBase + (Counter++ % 12u);
			FColor Color = FColor::Yellow;
			switch (Feedback.Outcome)
			{
			case EBH_HitOutcome::Block: Color = FColor::Cyan; break;
			case EBH_HitOutcome::PerfectBlock: Color = FColor::Green; break;
			case EBH_HitOutcome::PostureBreak: Color = FColor::Orange; break;
			default: Color = Feedback.bFatal ? FColor::Red : FColor::Yellow; break;
			}
			GEngine->AddOnScreenDebugMessage(Key, 2.f, Color, Line);
		}
	}
#endif
}

UBH_HitFeedbackSubsystem* UBH_HitFeedbackSubsystem::GetHitFeedbackSubsystem(const UObject* WorldContext)
{
	const UWorld* ContextWorld = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	return ContextWorld ? ContextWorld->GetSubsystem<UBH_HitFeedbackSubsystem>() : nullptr;
}

void UBH_HitFeedbackSubsystem::BroadcastFeedback(const FBH_HitFeedback& Feedback)
{
	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
#if !UE_BUILD_SHIPPING
	if (CVarBHHitFeedbackDebug.GetValueOnGameThread() != 0)
	{
		DebugReport(World, Feedback);
	}
#endif
	OnHitFeedback.Broadcast(Feedback);
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
	Hub->BroadcastFeedback(Feedback);
}

EBH_HitSurface UBH_HitFeedbackLibrary::ResolveSurface(const EBH_HitSurface EnemyLevel, const EBH_HitSurface IdentityLevel)
{
	// An explicit (non-Flesh) enemy-level value is the designer's statement about the body and wins; Flesh is the default and
	// therefore "not set", so the identity component's value is used then.
	return EnemyLevel != EBH_HitSurface::Flesh ? EnemyLevel : IdentityLevel;
}

EBH_HitSurface UBH_HitFeedbackLibrary::GetHitSurface(const AActor* Actor)
{
	EBH_HitSurface EnemyLevel = EBH_HitSurface::Flesh;
	if (const ABH_EnemyBase* Enemy = Cast<ABH_EnemyBase>(Actor))
	{
		EnemyLevel = Enemy->SurfaceType;
	}
	EBH_HitSurface IdentityLevel = EBH_HitSurface::Flesh;
	if (const UBH_CombatIdentityComponent* Identity = UBH_CombatIdentityComponent::Find(Actor))
	{
		IdentityLevel = Identity->SurfaceType;
	}
	return ResolveSurface(EnemyLevel, IdentityLevel);
}

bool UBH_HitFeedbackLibrary::IsActorBlighted(const AActor* Actor)
{
	// Either source saying "Blighted" is enough (false is the default, not a statement).
	if (const ABH_EnemyBase* Enemy = Cast<ABH_EnemyBase>(Actor))
	{
		if (Enemy->bBlighted)
		{
			return true;
		}
	}
	if (const UBH_CombatIdentityComponent* Identity = UBH_CombatIdentityComponent::Find(Actor))
	{
		return Identity->bBlighted;
	}
	return false;
}

#if !UE_BUILD_SHIPPING
// bh.HitFeedback.Test [Hit|Block|PerfectBlock|PostureBreak] [Light|Medium|Heavy|Massive] [Flesh|Armour|Shell] [Blighted] [Fatal]
// Fires one fake OnHitFeedback on THIS machine's world with the local player's pawn as both attacker and victim, so a Blueprint listener
// (sound / sparks / shake / rumble) can be exercised without an enemy. Both bAttackerLocallyControlled and bVictimLocallyControlled are true.
static void BH_HitFeedbackTestCommand(const TArray<FString>& Args, UWorld* World)
{
	if (!World)
	{
		return;
	}
	UBH_HitFeedbackSubsystem* Hub = World->GetSubsystem<UBH_HitFeedbackSubsystem>();
	if (!Hub)
	{
		UE_LOG(LogBHHitFeedback, Warning, TEXT("bh.HitFeedback.Test: no HitFeedback subsystem in %s"), *World->GetName());
		return;
	}

	APlayerController* PC = World->GetFirstPlayerController();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;

	FBH_HitFeedback Feedback;
	Feedback.Attacker = Pawn;
	Feedback.Victim = Pawn;
	Feedback.Tier = EBH_ImpactTier::Medium;
	Feedback.Surface = EBH_HitSurface::Flesh;
	Feedback.Outcome = EBH_HitOutcome::Hit;
	Feedback.bAttackerLocallyControlled = IsLocallyControlledPawn(Pawn);
	Feedback.bVictimLocallyControlled = IsLocallyControlledPawn(Pawn);
	if (Pawn)
	{
		Feedback.Location = Pawn->GetActorLocation() + FVector(0.0, 0.0, 60.0);
		Feedback.Direction = Pawn->GetActorForwardVector();
	}

	for (const FString& Arg : Args)
	{
		int64 Value = INDEX_NONE;
		if (Arg.Equals(TEXT("Blighted"), ESearchCase::IgnoreCase))
		{
			Feedback.bIsBlighted = true;
		}
		else if (Arg.Equals(TEXT("Fatal"), ESearchCase::IgnoreCase))
		{
			Feedback.bFatal = true;
		}
		else if ((Value = StaticEnum<EBH_HitOutcome>()->GetValueByNameString(Arg)) != INDEX_NONE)
		{
			Feedback.Outcome = static_cast<EBH_HitOutcome>(Value);
		}
		else if ((Value = StaticEnum<EBH_ImpactTier>()->GetValueByNameString(Arg)) != INDEX_NONE)
		{
			Feedback.Tier = static_cast<EBH_ImpactTier>(Value);
		}
		else if ((Value = StaticEnum<EBH_HitSurface>()->GetValueByNameString(Arg)) != INDEX_NONE)
		{
			Feedback.Surface = static_cast<EBH_HitSurface>(Value);
		}
		else
		{
			UE_LOG(LogBHHitFeedback, Warning, TEXT("bh.HitFeedback.Test: ignoring unknown argument '%s'"), *Arg);
		}
	}

	UE_LOG(LogBHHitFeedback, Log, TEXT("bh.HitFeedback.Test: firing a fake event in %s (pawn=%s)"), *World->GetName(), *GetNameSafe(Pawn));
	Hub->BroadcastFeedback(Feedback);
}

static FAutoConsoleCommandWithWorldAndArgs GBHHitFeedbackTestCommand(
	TEXT("bh.HitFeedback.Test"),
	TEXT("Fires a fake OnHitFeedback on the local player. Args (any order): Hit|Block|PerfectBlock|PostureBreak, Light|Medium|Heavy|Massive, Flesh|Armour|Shell, Blighted, Fatal."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&BH_HitFeedbackTestCommand));
#endif
