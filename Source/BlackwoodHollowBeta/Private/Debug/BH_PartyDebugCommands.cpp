// Blackwood Hollow - Phase 11F / 11G party debug console commands
//
// Server-authoritative like every bh.* command: they go through the LOCAL player's UBH_PlayerDeathComponent debug RPCs, so they run
// directly on the host / standalone and are routed to the server from a client window.
//   bh.Party.Wipe [kill]      runs the party-wipe consequences now (every enemy resets to full Health / Posture, aggro cleared, back at its
//                             spawn; the current wave restarts, cleared waves stay cleared). With "kill" the whole party is killed first (a real wipe).
//   bh.Fracture.Apply [party] fractures the local player (no stacking: already fractured = nothing). "party" = every party member.
//   bh.Fracture.Remove [party] removes Fracture from the local player (or "party": every member), no shards needed.

#include "Player/BH_PlayerDeathComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

#if !UE_BUILD_SHIPPING

DEFINE_LOG_CATEGORY_STATIC(LogBHPartyDebug, Log, All);

namespace BH_PartyDebugCommands_Private
{
	static void Notify(const UWorld* World, const TCHAR* CommandName, const FString& Reason)
	{
		UE_LOG(LogBHPartyDebug, Warning, TEXT("%s: %s"), CommandName, *Reason);
		if (GEngine && World && World->GetNetMode() == NM_Client)
		{
			GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Yellow, FString::Printf(TEXT("%s: %s"), CommandName, *Reason));
		}
	}

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

	static bool HasArg(const TArray<FString>& Args, const TCHAR* Wanted)
	{
		for (const FString& Arg : Args)
		{
			if (Arg.Equals(Wanted, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	static FAutoConsoleCommandWithWorldAndArgs CmdPartyWipe(
		TEXT("bh.Party.Wipe"),
		TEXT("Runs the party-wipe consequences now (enemies reset, current wave restarts). 'bh.Party.Wipe kill' kills the whole party first. Works from a client window (server RPC)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UBH_PlayerDeathComponent* Comp = ResolveLocalDeathComponent(World, TEXT("bh.Party.Wipe")))
			{
				Comp->ServerDebugWipe(HasArg(Args, TEXT("kill")));
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdFractureApply(
		TEXT("bh.Fracture.Apply"),
		TEXT("Applies Fracture to the local player ('bh.Fracture.Apply party' = every party member). Already fractured = nothing. Works from a client window (server RPC)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UBH_PlayerDeathComponent* Comp = ResolveLocalDeathComponent(World, TEXT("bh.Fracture.Apply")))
			{
				Comp->ServerDebugFracture(/*bApply*/ true, HasArg(Args, TEXT("party")));
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdFractureRemove(
		TEXT("bh.Fracture.Remove"),
		TEXT("Removes Fracture from the local player ('bh.Fracture.Remove party' = every party member), no shards needed. Works from a client window (server RPC)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (UBH_PlayerDeathComponent* Comp = ResolveLocalDeathComponent(World, TEXT("bh.Fracture.Remove")))
			{
				Comp->ServerDebugFracture(/*bApply*/ false, HasArg(Args, TEXT("party")));
			}
		}));
}

#endif // !UE_BUILD_SHIPPING
