// Blackwood Hollow - Phase 12G enemy debug console commands
//
// Spawn / kill go through the LOCAL player's PlayerState server RPCs, so they work the same in the host window (the RPC runs locally) and in a
// client window (the RPC runs on the server). Compiled out in Shipping.
//   bh.Enemy.Spawn <ShortName> [Rank] [Level]   ShortName: Crab, ArmouredCrab, CV, CVSergeant, EliteCrab (UBH_RPGSettings::DebugEnemyClasses).
//                                               Rank: Common|Veteran|Elite|Boss (default: the Blueprint's). Level: integer (default 1).
//                                               Spawns 5 m in front of the issuing player.
//   bh.Enemy.KillAll [RadiusMeters]             kills every living enemy within the radius (default 60 m; 0 = the whole world) through the real death path.
//   bh.Enemy.ShowRank [0|1]                     toggles a floating label per enemy on THIS machine: rank / archetype / level / health / posture / XP.
//   bh.Enemy.List                               logs one line per enemy as THIS machine sees it (use it in the client window to read the client copy).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AbilitySystemComponent.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "Combat/BH_EnemyRank.h"
#include "Containers/Ticker.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Player/BH_PlayerState.h"

DEFINE_LOG_CATEGORY_STATIC(LogBHEnemyDebug, Log, All);

namespace BH_EnemyDebugCommands_Private
{
	static bool GShowRank = false;
	static FTSTicker::FDelegateHandle GShowRankHandle;

	static void Notify(const UWorld* World, const TCHAR* CommandName, const FString& Message)
	{
		UE_LOG(LogBHEnemyDebug, Warning, TEXT("%s: %s"), CommandName, *Message);
		if (GEngine && World)
		{
			GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Yellow, FString::Printf(TEXT("%s: %s"), CommandName, *Message));
		}
	}

	static ABH_PlayerState* GetLocalPlayerState(const UWorld* World, const TCHAR* CommandName)
	{
		const APlayerController* LocalPC = World ? World->GetFirstPlayerController() : nullptr;
		ABH_PlayerState* BHState = LocalPC ? LocalPC->GetPlayerState<ABH_PlayerState>() : nullptr;
		if (!BHState)
		{
			Notify(World, CommandName, TEXT("refused, no local BH player state yet."));
		}
		return BHState;
	}

	static float AttributeOf(const UAbilitySystemComponent* ASC, const FGameplayAttribute& Attribute)
	{
		return (ASC && ASC->HasAttributeSetForAttribute(Attribute)) ? ASC->GetNumericAttribute(Attribute) : 0.f;
	}

	/** One line describing an enemy as this machine sees it. */
	static FString Describe(const APawn* Enemy)
	{
		const UAbilitySystemComponent* ASC = Enemy->FindComponentByClass<UAbilitySystemComponent>();
		return FString::Printf(TEXT("%s [%s/%s] L%d HP %.1f/%.1f Posture %.1f/%.1f AttackPower %.1f DmgMult %.2f XP %d%s"),
			*Enemy->GetName(), UBH_EnemyRankLibrary::RankName(UBH_EnemyRankLibrary::GetRank(Enemy)), UBH_EnemyRankLibrary::ArchetypeName(UBH_EnemyRankLibrary::GetArchetype(Enemy)),
			UBH_EnemyRankLibrary::GetEnemyLevel(Enemy),
			AttributeOf(ASC, UAH_AttributeSet::GetHealthAttribute()), AttributeOf(ASC, UAH_AttributeSet::GetMaxHealthAttribute()),
			AttributeOf(ASC, UAH_AttributeSet::GetPostureAttribute()), AttributeOf(ASC, UAH_AttributeSet::GetMaxPostureAttribute()),
			AttributeOf(ASC, UAH_AttributeSet::GetAttackPowerAttribute()), UBH_EnemyRankLibrary::GetDamageMultiplier(Enemy),
			UBH_EnemyRankLibrary::GetEnemyXPReward(Enemy),
			(ASC && ASC->HasMatchingGameplayTag(TAG_State_Combat_Dead)) ? TEXT(" DEAD") : TEXT(""));
	}

	static bool TickShowRank(float /*DeltaTime*/)
	{
		if (!GShowRank || !GEngine)
		{
			GShowRankHandle.Reset();
			return false; // removes the ticker
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (!World || (Context.WorldType != EWorldType::Game && Context.WorldType != EWorldType::PIE))
			{
				continue;
			}
			for (TActorIterator<APawn> It(World); It; ++It)
			{
				const APawn* Enemy = *It;
				if (!UBH_EnemyRankLibrary::HasRankData(Enemy))
				{
					continue;
				}
				DrawDebugString(World, Enemy->GetActorLocation() + FVector(0.0, 0.0, 170.0), Describe(Enemy), nullptr, FColor::Orange, 0.f, true, 1.f);
			}
		}
		return true;
	}

	static void SetShowRank(bool bShow)
	{
		GShowRank = bShow;
		if (bShow && !GShowRankHandle.IsValid())
		{
			GShowRankHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickShowRank));
		}
	}

	static FAutoConsoleCommandWithWorldAndArgs CmdSpawn(
		TEXT("bh.Enemy.Spawn"),
		TEXT("bh.Enemy.Spawn <ShortName> [Rank] [Level] - spawns an enemy 5 m in front of you (server). ShortName: Crab, ArmouredCrab, CV, CVSergeant, EliteCrab. Rank: Common|Veteran|Elite|Boss."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const TCHAR* CommandName = TEXT("bh.Enemy.Spawn");
			if (Args.Num() < 1)
			{
				Notify(World, CommandName, TEXT("usage: bh.Enemy.Spawn <Crab|ArmouredCrab|CV|CVSergeant|EliteCrab> [Common|Veteran|Elite|Boss] [Level]"));
				return;
			}
			int32 RankOverride = -1;
			int32 LevelOverride = 0;
			for (int32 Index = 1; Index < Args.Num(); ++Index)
			{
				EBH_EnemyRank ParsedRank = EBH_EnemyRank::Common;
				if (UBH_EnemyRankLibrary::ParseRank(Args[Index], ParsedRank))
				{
					RankOverride = static_cast<int32>(ParsedRank);
				}
				else if (Args[Index].IsNumeric())
				{
					LevelOverride = FCString::Atoi(*Args[Index]);
				}
				else
				{
					Notify(World, CommandName, FString::Printf(TEXT("ignored unknown argument '%s'."), *Args[Index]));
				}
			}
			if (ABH_PlayerState* BHState = GetLocalPlayerState(World, CommandName))
			{
				BHState->ServerDebugSpawnEnemy(Args[0], RankOverride, FMath::Clamp(LevelOverride, 0, ABH_PlayerState::MaxDebugLevel));
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdKillAll(
		TEXT("bh.Enemy.KillAll"),
		TEXT("bh.Enemy.KillAll [RadiusMeters=60] - kills every living enemy within the radius of you (0 = whole world) through the real death path (server)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const float RadiusMeters = Args.Num() >= 1 ? FCString::Atof(*Args[0]) : 60.f;
			if (ABH_PlayerState* BHState = GetLocalPlayerState(World, TEXT("bh.Enemy.KillAll")))
			{
				BHState->ServerDebugKillAllEnemies(FMath::Clamp(RadiusMeters, 0.f, 10000.f) * 100.f);
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdShowRank(
		TEXT("bh.Enemy.ShowRank"),
		TEXT("bh.Enemy.ShowRank [0|1] - floating rank / archetype / level / health / posture / XP label above every enemy on this machine."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const bool bShow = Args.Num() >= 1 ? FCString::Atoi(*Args[0]) != 0 : !GShowRank;
			SetShowRank(bShow);
			Notify(World, TEXT("bh.Enemy.ShowRank"), bShow ? TEXT("on") : TEXT("off"));
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdList(
		TEXT("bh.Enemy.List"),
		TEXT("bh.Enemy.List - logs one line per enemy as this machine sees it (run it in a client window to read the client copy)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World)
			{
				return;
			}
			int32 Count = 0;
			for (TActorIterator<APawn> It(World); It; ++It)
			{
				if (UBH_EnemyRankLibrary::HasRankData(*It))
				{
					UE_LOG(LogBHEnemyDebug, Warning, TEXT("[%s] %s"), World->GetNetMode() == NM_Client ? TEXT("client") : TEXT("server"), *Describe(*It));
					++Count;
				}
			}
			Notify(World, TEXT("bh.Enemy.List"), FString::Printf(TEXT("%d enemies (see the log)."), Count));
		}));
}

#endif // !UE_BUILD_SHIPPING
