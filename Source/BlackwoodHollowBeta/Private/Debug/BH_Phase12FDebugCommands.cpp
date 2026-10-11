// Blackwood Hollow - Phase 12F progression-gate debug console commands
//
// All go through the LOCAL player's PlayerState / Interactor debug RPCs, so they work the same in the host / standalone window (the RPC runs
// locally) and in a client window (the RPC runs on the server). Compiled out in Shipping.
//   bh.Loadout.UnlockSetB          unlock weapon set B for the local player (the Island 1 start locks it).
//   bh.Skills.GrantBurst           teach the local player Overload Burst as if they touched the Warden obelisk (equips into the first slot that accepts it).
//   bh.World.OpenSpanGate          set the world flag Island1.SpanGateOpen (opens every ABH_WorldGate keyed to it).
//   bh.Interact.Complete [Index]   the server completes the interactable the player at Index (0 = host, default 0) is focused on, skipping the hold /
//                                  range / facing checks. In a CLIENT window the index is ignored: it completes the local player's own focus.

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Interaction/BH_InteractableComponent.h"
#include "Interaction/BH_InteractorComponent.h"
#include "Player/BH_PlayerState.h"

#if !UE_BUILD_SHIPPING

DEFINE_LOG_CATEGORY_STATIC(LogBHPhase12FDebug, Log, All);

namespace BH_Phase12FDebugCommands_Private
{
	static void Notify(const UWorld* World, const TCHAR* CommandName, const FString& Message)
	{
		UE_LOG(LogBHPhase12FDebug, Warning, TEXT("%s: %s"), CommandName, *Message);
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

	static FAutoConsoleCommandWithWorld CmdUnlockSetB(
		TEXT("bh.Loadout.UnlockSetB"),
		TEXT("Unlocks weapon set B for the local player (server grant)."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (ABH_PlayerState* BHState = GetLocalPlayerState(World, TEXT("bh.Loadout.UnlockSetB")))
			{
				BHState->ServerDebugUnlockSetB();
			}
		}));

	static FAutoConsoleCommandWithWorld CmdGrantBurst(
		TEXT("bh.Skills.GrantBurst"),
		TEXT("Teaches the local player Overload Burst, as if they touched the Warden obelisk (server grant)."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (ABH_PlayerState* BHState = GetLocalPlayerState(World, TEXT("bh.Skills.GrantBurst")))
			{
				BHState->ServerDebugGrantBurst();
			}
		}));

	static FAutoConsoleCommandWithWorld CmdOpenSpanGate(
		TEXT("bh.World.OpenSpanGate"),
		TEXT("Sets the world flag Island1.SpanGateOpen (server), opening the Span Gate."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (ABH_PlayerState* BHState = GetLocalPlayerState(World, TEXT("bh.World.OpenSpanGate")))
			{
				BHState->ServerDebugOpenSpanGate();
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdInteractComplete(
		TEXT("bh.Interact.Complete"),
		TEXT("bh.Interact.Complete [PlayerIndex] - the server completes the interactable that player's interactor is focused on (no hold, no range check). Client windows always use their own player."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const TCHAR* CommandName = TEXT("bh.Interact.Complete");
			if (!World)
			{
				return;
			}

			UBH_InteractorComponent* Interactor = nullptr;
			if (World->GetNetMode() == NM_Client)
			{
				// A client can only drive its own player; the RPC carries the focus to the server.
				const APlayerController* LocalPC = World->GetFirstPlayerController();
				Interactor = LocalPC ? UBH_InteractorComponent::Find(LocalPC->GetPawn()) : nullptr;
			}
			else
			{
				const int32 WantedIndex = Args.Num() >= 1 ? FCString::Atoi(*Args[0]) : 0;
				int32 Index = 0;
				for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
				{
					if (Index == WantedIndex)
					{
						const APlayerController* PC = It->Get();
						Interactor = PC ? UBH_InteractorComponent::Find(PC->GetPawn()) : nullptr;
						break;
					}
					++Index;
				}
			}

			if (!Interactor)
			{
				Notify(World, CommandName, TEXT("refused, no player pawn with an interactor at that index."));
				return;
			}
			Interactor->ServerDebugCompleteInteraction(Interactor->GetFocus());
		}));
}

#endif // !UE_BUILD_SHIPPING
