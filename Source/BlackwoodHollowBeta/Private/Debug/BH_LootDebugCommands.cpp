// Blackwood Hollow - Phase 11C loot debug console commands
//
// Both go through the LOCAL player's UBH_InteractorComponent debug RPCs, so they work the same typed in the host / standalone window
// (the RPC runs locally) and in a client window (the RPC runs on the server). Compiled out in Shipping.
//   bh.Loot.Give <item> <count>   give <count> of <item> to the local player. <item> = Shard | Sap | Incense, or an item Blueprint class path
//                                 (/Game/.../BI_Foo.BI_Foo_C). Count 1..999.
//   bh.Loot.ResetContainers       put every loot container (opened + per-player "looted" list), harvest node (regrow now) and loose pickup
//                                 in the world back to fresh. Not tied to a party wipe: only this command resets them.

#include "Interaction/BH_InteractorComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

#if !UE_BUILD_SHIPPING

DEFINE_LOG_CATEGORY_STATIC(LogBHLootDebug, Log, All);

namespace BH_LootDebugCommands_Private
{
	static void Notify(const UWorld* World, const TCHAR* CommandName, const FString& Reason)
	{
		UE_LOG(LogBHLootDebug, Warning, TEXT("%s: %s"), CommandName, *Reason);
		if (GEngine && World)
		{
			GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Yellow, FString::Printf(TEXT("%s: %s"), CommandName, *Reason));
		}
	}

	static UBH_InteractorComponent* GetLocalInteractor(const UWorld* World, const TCHAR* CommandName)
	{
		const APlayerController* LocalPC = World ? World->GetFirstPlayerController() : nullptr;
		UBH_InteractorComponent* Interactor = LocalPC ? UBH_InteractorComponent::Find(LocalPC->GetPawn()) : nullptr;
		if (!Interactor)
		{
			Notify(World, CommandName, TEXT("refused, no local player pawn with an interactor yet."));
		}
		return Interactor;
	}

	static FAutoConsoleCommandWithWorldAndArgs CmdLootGive(
		TEXT("bh.Loot.Give"),
		TEXT("bh.Loot.Give <Shard|Sap|Incense|class path> <count> - gives the local player that item (server grant into the PlayerState inventory)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() < 1)
			{
				Notify(World, TEXT("bh.Loot.Give"), TEXT("Usage: bh.Loot.Give <Shard|Sap|Incense|class path> <count>"));
				return;
			}
			const int32 Count = Args.Num() >= 2 ? FCString::Atoi(*Args[1]) : 1;
			if (Count < 1 || Count > 999)
			{
				Notify(World, TEXT("bh.Loot.Give"), TEXT("refused, count must be 1..999."));
				return;
			}
			if (UBH_InteractorComponent* Interactor = GetLocalInteractor(World, TEXT("bh.Loot.Give")))
			{
				Interactor->ServerDebugGiveItem(Args[0], Count);
			}
		}));

	static FAutoConsoleCommandWithWorld CmdLootReset(
		TEXT("bh.Loot.ResetContainers"),
		TEXT("Server: resets every loot container, harvest node and loose pickup in the world to fresh."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (UBH_InteractorComponent* Interactor = GetLocalInteractor(World, TEXT("bh.Loot.ResetContainers")))
			{
				Interactor->ServerDebugResetLoot();
			}
		}));
}

#endif // !UE_BUILD_SHIPPING
