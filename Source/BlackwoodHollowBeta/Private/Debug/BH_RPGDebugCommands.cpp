// Blackwood Hollow - Phase 9 RPG / arena debug console commands
//
// All of them are server-authoritative: they run in the world they are typed into and refuse in a pure client world
// (NM_Client), so in a PIE listen server type them in the server (host) window.
//   bh.Arena.SpawnBoss        spawn the boss of every wave spawner in the world (found through bIsBoss on the entry classes)
//   bh.Arena.SkipToWave N     abandon the current wave and start wave N (1 = first) on every wave spawner
//   bh.XP.Grant N             give N XP to every player in the world
//   bh.Level.Set N            set every player's level to N (XP resets, stats and Health/Posture/Stamina are re-applied)

#include "AI/BH_EnemyWaveSpawner.h"
#include "Progression/BH_ProgressionComponent.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

#if !UE_BUILD_SHIPPING

namespace BH_RPGDebugCommands_Private
{
	/** True (and logs nothing) in a world that owns the game state; a client world logs why the command did nothing. */
	static bool IsAuthoritativeWorld(const UWorld* World, const TCHAR* CommandName)
	{
		if (!World)
		{
			return false;
		}
		if (World->GetNetMode() == NM_Client)
		{
			UE_LOG(LogBHCombat, Warning, TEXT("%s: server only (run it in the listen-server / standalone window)."), CommandName);
			return false;
		}
		return true;
	}

	static void ForEachProgression(UWorld* World, TFunctionRef<void(UBH_ProgressionComponent&)> Fn)
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			const APlayerController* PC = It->Get();
			if (UBH_ProgressionComponent* Progression = UBH_ProgressionComponent::FindProgression(PC))
			{
				Fn(*Progression);
			}
		}
	}

	static FAutoConsoleCommandWithWorld CmdSpawnBoss(
		TEXT("bh.Arena.SpawnBoss"),
		TEXT("Server: spawns the boss (the wave entry whose identity component has bIsBoss) from every wave spawner in the world."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (!IsAuthoritativeWorld(World, TEXT("bh.Arena.SpawnBoss")))
			{
				return;
			}
			int32 Count = 0;
			for (TActorIterator<ABH_EnemyWaveSpawner> It(World); It; ++It)
			{
				It->DebugSpawnBoss();
				++Count;
			}
			UE_LOG(LogBHCombat, Log, TEXT("bh.Arena.SpawnBoss: %d wave spawner(s)."), Count);
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdSkipToWave(
		TEXT("bh.Arena.SkipToWave"),
		TEXT("bh.Arena.SkipToWave N - server: abandons the current wave and starts wave N (1 = the first) on every wave spawner."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!IsAuthoritativeWorld(World, TEXT("bh.Arena.SkipToWave")))
			{
				return;
			}
			if (Args.IsEmpty())
			{
				UE_LOG(LogBHCombat, Warning, TEXT("Usage: bh.Arena.SkipToWave N (1 = the first wave)"));
				return;
			}
			const int32 WaveNumber = FCString::Atoi(*Args[0]);
			for (TActorIterator<ABH_EnemyWaveSpawner> It(World); It; ++It)
			{
				It->DebugSkipToWave(WaveNumber);
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdGrantXP(
		TEXT("bh.XP.Grant"),
		TEXT("bh.XP.Grant N - server: gives N XP to every player (levels up as needed)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!IsAuthoritativeWorld(World, TEXT("bh.XP.Grant")))
			{
				return;
			}
			if (Args.IsEmpty())
			{
				UE_LOG(LogBHCombat, Warning, TEXT("Usage: bh.XP.Grant N"));
				return;
			}
			const int32 Amount = FCString::Atoi(*Args[0]);
			ForEachProgression(World, [Amount](UBH_ProgressionComponent& Progression)
			{
				Progression.AddXP(Amount);
				UE_LOG(LogBHCombat, Log, TEXT("bh.XP.Grant: +%d XP -> level %d, %d / %d XP."), Amount, Progression.GetLevel(), Progression.GetCurrentXP(), Progression.GetXPToNextLevel());
			});
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdSetLevel(
		TEXT("bh.Level.Set"),
		TEXT("bh.Level.Set N - server: sets every player's level to N (clamped to 1..MaxLevel), XP resets, stats are re-applied."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!IsAuthoritativeWorld(World, TEXT("bh.Level.Set")))
			{
				return;
			}
			if (Args.IsEmpty())
			{
				UE_LOG(LogBHCombat, Warning, TEXT("Usage: bh.Level.Set N"));
				return;
			}
			const int32 NewLevel = FCString::Atoi(*Args[0]);
			ForEachProgression(World, [NewLevel](UBH_ProgressionComponent& Progression)
			{
				Progression.SetLevel(NewLevel);
				UE_LOG(LogBHCombat, Log, TEXT("bh.Level.Set: level %d."), Progression.GetLevel());
			});
		}));
}

#endif // !UE_BUILD_SHIPPING
