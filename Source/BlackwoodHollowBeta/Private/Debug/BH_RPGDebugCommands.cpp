// Blackwood Hollow - Phase 9 RPG / arena debug console commands
//
// All of them are server-authoritative. Typed in the host / standalone window they run directly. Typed in a client window they
// are routed through the local PlayerController's ABH_PlayerState debug RPC (ServerDebug*) and run on the server; a client that
// cannot route (no PlayerState yet) or passes an out-of-range value is refused with a log line and a yellow on-screen message.
//   bh.Arena.SpawnBoss        spawn the boss of every wave spawner in the world (found through bIsBoss on the entry classes)
//   bh.Arena.SkipToWave N     abandon the current wave and start wave N (1 = first) on every wave spawner
//   bh.XP.Grant N             give N XP to every player in the world
//   bh.Level.Set N            set every player's level to N (XP resets, stats and Health/Posture/Stamina are re-applied)
//   bh.Player.Kill           (Phase 10B part 2) kill the LOCAL player's pawn: starts the death / ragdoll / revive-window flow
//   bh.Player.Revive         revive the LOCAL player's downed pawn in place right now (UBH_PlayerDeathComponent debug RPCs, so they work from a client window too)
//   bh.Move.StopTrace [0|1]  (Phase 11A-2) LOCAL, no server involved: toggles a trace that logs / prints the stop after the move input is released

#include "AI/BH_EnemyWaveSpawner.h"
#include "Player/BH_PlayerState.h"
#include "Player/BH_PlayerDeathComponent.h"
#include "Progression/BH_ProgressionComponent.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "Characters/BH_CharacterBase.h"
#include "Characters/BH_StanceMovementProfile.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
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

	/** The local player's death component (first local controller's pawn), or null with a logged / on-screen reason. */
	static UBH_PlayerDeathComponent* ResolveLocalDeathComponent(const UWorld* World, const TCHAR* CommandName)
	{
		const APlayerController* LocalPC = World ? World->GetFirstPlayerController() : nullptr;
		UBH_PlayerDeathComponent* Comp = UBH_PlayerDeathComponent::Find(LocalPC ? LocalPC->GetPawn() : nullptr);
		if (!Comp)
		{
			Notify(World, CommandName, TEXT("refused, the local player has no pawn yet."));
		}
		return Comp;
	}

	// The commands call the component's Server RPC: on the host / standalone it runs directly, from a client window it is sent to the server.
	static FAutoConsoleCommandWithWorld CmdKillPlayer(
		TEXT("bh.Player.Kill"),
		TEXT("Kills the local player's pawn through lethal damage (starts the death / ragdoll / revive flow). Works from a client window (server RPC)."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (UBH_PlayerDeathComponent* Comp = ResolveLocalDeathComponent(World, TEXT("bh.Player.Kill")))
			{
				Comp->ServerDebugKill();
			}
		}));

	static FAutoConsoleCommandWithWorld CmdRevivePlayer(
		TEXT("bh.Player.Revive"),
		TEXT("Instantly revives the local player's downed pawn in place at the revive health percent (skips the hold and the combat check). Works from a client window (server RPC)."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (UBH_PlayerDeathComponent* Comp = ResolveLocalDeathComponent(World, TEXT("bh.Player.Revive")))
			{
				Comp->ServerDebugRevive();
			}
		}));

	// ------------------------------------------------------------------------------------------------------------------------
	// bh.Move.StopTrace (Phase 11A-2): how long and how far does the local pawn slide after the move input is released?
	// Purely local and read-only (it never touches movement), so it needs no server routing.
	// ------------------------------------------------------------------------------------------------------------------------

	static const TCHAR* StopTraceGaitName(EBH_Gait Gait)
	{
		switch (Gait)
		{
		case EBH_Gait::Walk:
			return TEXT("Walk");
		case EBH_Gait::Sprint:
			return TEXT("Sprint");
		default:
			return TEXT("Run");
		}
	}

	/** Core-ticker sampler (game thread). One world at a time: running the command in another window moves the trace there. */
	class FMoveStopTracer
	{
	public:
		bool IsActiveIn(const UWorld* InWorld) const
		{
			return TickHandle.IsValid() && TracedWorld.Get() == InWorld;
		}

		void Start(UWorld* InWorld)
		{
			Stop();
			TracedWorld = InWorld;
			TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FMoveStopTracer::Tick), 0.f);
		}

		void Stop()
		{
			if (TickHandle.IsValid())
			{
				FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
				TickHandle.Reset();
			}
			bHadInput = false;
			bTracking = false;
		}

	private:
		bool Tick(float /*DeltaTime*/)
		{
			UWorld* TraceWorld = TracedWorld.Get();
			if (!TraceWorld)
			{
				TickHandle.Reset(); // returning false removes the ticker
				return false;
			}

			const APlayerController* LocalPC = TraceWorld->GetFirstPlayerController();
			const ACharacter* Char = LocalPC ? Cast<ACharacter>(LocalPC->GetPawn()) : nullptr;
			const UCharacterMovementComponent* Move = Char ? Char->GetCharacterMovement() : nullptr;
			if (!Move)
			{
				bHadInput = false;
				bTracking = false;
				return true;
			}

			const FVector Velocity2D(Move->Velocity.X, Move->Velocity.Y, 0.0);
			const float Speed = static_cast<float>(Velocity2D.Size());
			const bool bInput = Move->GetCurrentAcceleration().SizeSquared2D() > 1.0;
			const double Now = TraceWorld->GetTimeSeconds();
			const FVector Location = Char->GetActorLocation();
			const ABH_CharacterBase* Base = Cast<ABH_CharacterBase>(Char);

			if (bInput)
			{
				// What the player was doing while holding the key (the gait flips to Walk on release, so remember it now).
				if (Base)
				{
					HeldGait = Base->GetGait();
					HeldStance = Base->GetWeaponStance().ToString();
					bHeldStrafe = Base->GetRotationMode() == EBH_RotationMode::Strafe;
				}
				bHeldCrouch = Char->bIsCrouched;
				if (bTracking)
				{
					UE_LOG(LogBHCombat, Log, TEXT("bh.Move.StopTrace: input resumed after %.2f s, trace dropped."), Now - StartTime);
					bTracking = false;
				}
			}
			else if (bTracking)
			{
				PathLength += static_cast<float>(FVector::Dist2D(Location, LastLocation));
				LastLocation = Location;
				if (SlowSeconds < 0.f && Speed < SlowThreshold)
				{
					SlowSeconds = static_cast<float>(Now - StartTime);
					SlowDistance = static_cast<float>(FVector::Dist2D(Location, StartLocation));
				}
				if (Speed < FullStopSpeed)
				{
					Report(Char, Move, Location, Now, /*bTimedOut*/ false);
					bTracking = false;
				}
				else if (Now - StartTime > MaxTraceSeconds)
				{
					Report(Char, Move, Location, Now, /*bTimedOut*/ true);
					bTracking = false;
				}
			}
			else if (bHadInput && Speed > StartMinSpeed)
			{
				// Input released while moving: start measuring.
				bTracking = true;
				StartTime = Now;
				StartLocation = Location;
				LastLocation = Location;
				PathLength = 0.f;
				StartSpeed = Speed;
				SlowSeconds = -1.f;
				SlowDistance = 0.f;
				SlowThreshold = 20.f;
				PlannedDecel = -1.f;
				PlannedBand = TEXT("-");

				ReleaseFacing = Char->GetActorForwardVector().GetSafeNormal2D();
				ReleaseForwardDot = static_cast<float>(FVector::DotProduct(Velocity2D.GetSafeNormal(), ReleaseFacing));

				if (const UBH_StanceMovementProfile* Profile = Base ? Base->GetMovementProfile() : nullptr)
				{
					const EBH_Gait Band = Profile->GetBrakingBandForSpeed(Speed);
					PlannedBand = StopTraceGaitName(Band);
					PlannedDecel = Profile->GetGaitSettings(Band).BrakingDecelerationNoInput;
					SlowThreshold = Profile->WalkStopSpeedThreshold;
				}
			}

			bHadInput = bInput;
			return true;
		}

		void Report(const ACharacter* Char, const UCharacterMovementComponent* Move, const FVector& EndLocation, double Now, bool bTimedOut) const
		{
			const FVector Displacement(EndLocation.X - StartLocation.X, EndLocation.Y - StartLocation.Y, 0.0);
			const FVector ReleaseRight(-ReleaseFacing.Y, ReleaseFacing.X, 0.0);
			const float Forward = static_cast<float>(FVector::DotProduct(Displacement, ReleaseFacing));
			const float Lateral = static_cast<float>(FVector::DotProduct(Displacement, ReleaseRight));
			const TCHAR* Direction = ReleaseForwardDot > 0.7f ? TEXT("forward") : (ReleaseForwardDot < -0.7f ? TEXT("backward") : TEXT("lateral/diagonal"));
			const float ExpectedDistance = PlannedDecel > 0.f ? (StartSpeed * StartSpeed) / (2.f * PlannedDecel) : -1.f;

			const FString Message = FString::Printf(
				TEXT("StopTrace%s | %s %s%s%s | %s (dot %.2f) | start %.0f cm/s | <%.0f cm/s after %.2f s / %.0f cm | full stop %.2f s / %.0f cm (path %.0f, fwd %.0f, lat %.0f) | planned brake %.0f (%s band) -> ~%.0f cm | CMC brakeDecel %.0f, groundFriction %.1f, brakingFrictionFactor %.2f, separateBrakingFriction %d"),
				bTimedOut ? TEXT(" [TIMED OUT, still moving]") : TEXT(""),
				*HeldStance, StopTraceGaitName(HeldGait), bHeldCrouch ? TEXT(" crouch") : TEXT(""), bHeldStrafe ? TEXT(" strafe") : TEXT(""),
				Direction, ReleaseForwardDot,
				StartSpeed,
				SlowThreshold, SlowSeconds, SlowDistance,
				static_cast<float>(Now - StartTime), static_cast<float>(Displacement.Size()), PathLength, Forward, Lateral,
				PlannedDecel, PlannedBand, ExpectedDistance,
				Move->BrakingDecelerationWalking, Move->GroundFriction, Move->BrakingFrictionFactor, Move->bUseSeparateBrakingFriction ? 1 : 0);

			UE_LOG(LogBHCombat, Log, TEXT("%s: %s"), *GetNameSafe(Char), *Message);
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 12.f, bTimedOut ? FColor::Orange : FColor::Cyan, Message);
			}
		}

		static constexpr float StartMinSpeed = 20.f;      // cm/s: slower releases are not a slide worth measuring
		static constexpr float FullStopSpeed = 1.f;       // cm/s: below this the pawn counts as stopped
		static constexpr double MaxTraceSeconds = 6.0;    // give up on a stop that never ends

		TWeakObjectPtr<UWorld> TracedWorld;
		FTSTicker::FDelegateHandle TickHandle;

		bool bHadInput = false;
		bool bTracking = false;

		// Captured while the key is held.
		EBH_Gait HeldGait = EBH_Gait::Run;
		FString HeldStance;
		bool bHeldStrafe = false;
		bool bHeldCrouch = false;

		// Captured at release.
		double StartTime = 0.0;
		FVector StartLocation = FVector::ZeroVector;
		FVector LastLocation = FVector::ZeroVector;
		FVector ReleaseFacing = FVector::ForwardVector;
		float ReleaseForwardDot = 1.f;
		float StartSpeed = 0.f;
		float PathLength = 0.f;
		float SlowThreshold = 20.f;
		float SlowSeconds = -1.f;
		float SlowDistance = 0.f;
		float PlannedDecel = -1.f;
		const TCHAR* PlannedBand = TEXT("-");
	};

	static FMoveStopTracer GMoveStopTracer;

	static FAutoConsoleCommandWithWorldAndArgs CmdMoveStopTrace(
		TEXT("bh.Move.StopTrace"),
		TEXT("bh.Move.StopTrace [0|1] - local trace: when the move input is released while moving, logs and prints stance / gait, start speed, time and distance to a full stop (forward vs lateral). No argument toggles it. Run it in the window you are playing in."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World)
			{
				return;
			}
			const bool bWantOn = Args.IsEmpty() ? !GMoveStopTracer.IsActiveIn(World) : (FCString::Atoi(*Args[0]) != 0);
			if (bWantOn)
			{
				GMoveStopTracer.Start(World);
			}
			else
			{
				GMoveStopTracer.Stop();
			}
			const FString Message = FString::Printf(TEXT("bh.Move.StopTrace: %s. Release the move key while moving to get a reading."), bWantOn ? TEXT("ON") : TEXT("OFF"));
			UE_LOG(LogBHCombat, Log, TEXT("%s"), *Message);
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Cyan, Message);
			}
		}));
}

#endif // !UE_BUILD_SHIPPING
