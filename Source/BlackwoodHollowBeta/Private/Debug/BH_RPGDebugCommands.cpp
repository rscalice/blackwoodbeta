// Blackwood Hollow - Phase 9 RPG / arena debug console commands
//
// All of them are server-authoritative. Typed in the host / standalone window they run directly. Typed in a client window they
// are routed through the local PlayerController's ABH_PlayerState debug RPC (ServerDebug*) and run on the server; a client that
// cannot route (no PlayerState yet) or passes an out-of-range value is refused with a log line and a yellow on-screen message.
//   bh.Arena.SpawnBoss        spawn the boss of every wave spawner in the world (found through bIsBoss on the entry classes)
//   bh.Arena.SkipToWave N     abandon the current wave and start wave N (1 = first) on every wave spawner
//   bh.XP.Grant N             give N XP to every player in the world
//   bh.Level.Set N            set every player's level to N (XP resets, stats and Health/Posture/Stamina are re-applied)

#include "AI/BH_EnemyWaveSpawner.h"
#include "Player/BH_PlayerState.h"
#include "Progression/BH_ProgressionComponent.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

#if !UE_BUILD_SHIPPING

namespace BH_RPGDebugCommands_Private
{
	enum class ERoute : uint8
	{
		Local,   // this world owns the game state: run the command directly (host / standalone)
		Remote,  // client world: send the PlayerState debug RPC
		Refused, // nothing was done
	};

	/** Logs a warning and, in a client world, also shows it on screen (yellow, 4 s). */
	static void Notify(const UWorld* World, const TCHAR* CommandName, const FString& Reason)
	{
		UE_LOG(LogBHCombat, Warning, TEXT("%s: %s"), CommandName, *Reason);
		if (GEngine && World && World->GetNetMode() == NM_Client)
		{
			GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Yellow, FString::Printf(TEXT("%s: %s"), CommandName, *Reason));
		}
	}

	/** Local in an authoritative world; Remote (OutPlayerState set) in a client world that has its PlayerState; otherwise Refused. */
	static ERoute ResolveRoute(const UWorld* World, const TCHAR* CommandName, ABH_PlayerState*& OutPlayerState)
	{
		OutPlayerState = nullptr;
		if (!World)
		{
			return ERoute::Refused;
		}
		if (World->GetNetMode() != NM_Client)
		{
			return ERoute::Local;
		}
		const APlayerController* LocalPC = World->GetFirstPlayerController();
		OutPlayerState = LocalPC ? LocalPC->GetPlayerState<ABH_PlayerState>() : nullptr;
		if (!OutPlayerState)
		{
			Notify(World, CommandName, TEXT("refused, the local PlayerState is not available yet (not connected to the server?)."));
			return ERoute::Refused;
		}
		return ERoute::Remote;
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
			ABH_PlayerState* RemotePS = nullptr;
			const ERoute Route = ResolveRoute(World, TEXT("bh.Arena.SpawnBoss"), RemotePS);
			if (Route == ERoute::Refused)
			{
				return;
			}
			if (Route == ERoute::Remote)
			{
				RemotePS->ServerDebugSpawnBoss();
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
			ABH_PlayerState* RemotePS = nullptr;
			const ERoute Route = ResolveRoute(World, TEXT("bh.Arena.SkipToWave"), RemotePS);
			if (Route == ERoute::Refused)
			{
				return;
			}
			if (Args.IsEmpty())
			{
				Notify(World, TEXT("bh.Arena.SkipToWave"), TEXT("Usage: bh.Arena.SkipToWave N (1 = the first wave)"));
				return;
			}
			const int32 WaveNumber = FCString::Atoi(*Args[0]);
			if (Route == ERoute::Remote)
			{
				if (WaveNumber < 1 || WaveNumber > ABH_PlayerState::MaxDebugWave)
				{
					Notify(World, TEXT("bh.Arena.SkipToWave"), FString::Printf(TEXT("refused, N must be 1..%d."), ABH_PlayerState::MaxDebugWave));
					return;
				}
				RemotePS->ServerDebugSkipToWave(WaveNumber);
				return;
			}
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
			ABH_PlayerState* RemotePS = nullptr;
			const ERoute Route = ResolveRoute(World, TEXT("bh.XP.Grant"), RemotePS);
			if (Route == ERoute::Refused)
			{
				return;
			}
			if (Args.IsEmpty())
			{
				Notify(World, TEXT("bh.XP.Grant"), TEXT("Usage: bh.XP.Grant N"));
				return;
			}
			const int32 Amount = FCString::Atoi(*Args[0]);
			if (Route == ERoute::Remote)
			{
				if (Amount < 0 || Amount > ABH_PlayerState::MaxDebugXP)
				{
					Notify(World, TEXT("bh.XP.Grant"), FString::Printf(TEXT("refused, N must be 0..%d."), ABH_PlayerState::MaxDebugXP));
					return;
				}
				RemotePS->ServerDebugGrantXP(Amount);
				return;
			}
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
			ABH_PlayerState* RemotePS = nullptr;
			const ERoute Route = ResolveRoute(World, TEXT("bh.Level.Set"), RemotePS);
			if (Route == ERoute::Refused)
			{
				return;
			}
			if (Args.IsEmpty())
			{
				Notify(World, TEXT("bh.Level.Set"), TEXT("Usage: bh.Level.Set N"));
				return;
			}
			const int32 NewLevel = FCString::Atoi(*Args[0]);
			if (Route == ERoute::Remote)
			{
				if (NewLevel < 1 || NewLevel > ABH_PlayerState::MaxDebugLevel)
				{
					Notify(World, TEXT("bh.Level.Set"), FString::Printf(TEXT("refused, N must be 1..%d."), ABH_PlayerState::MaxDebugLevel));
					return;
				}
				RemotePS->ServerDebugSetLevel(NewLevel);
				return;
			}
			ForEachProgression(World, [NewLevel](UBH_ProgressionComponent& Progression)
			{
				Progression.SetLevel(NewLevel);
				UE_LOG(LogBHCombat, Log, TEXT("bh.Level.Set: level %d."), Progression.GetLevel());
			});
		}));
}

#endif // !UE_BUILD_SHIPPING
