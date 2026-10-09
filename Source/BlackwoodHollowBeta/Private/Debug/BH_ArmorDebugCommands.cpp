// Blackwood Hollow - Phase 11E armor debug console command
//
// Goes through the LOCAL player's UBH_ArmorVisualComponent debug RPC, so it works the same typed in the host / standalone window (the RPC runs
// locally) and in a client window (the RPC runs on the server). Compiled out in Shipping.
//   bh.Armor.Give <Light|Medium|Heavy|Clear>   grants (once) and equips the whole armor set for the local player, taking off any other armor.
//                                              Clear takes all armor off (the starting outfit shows). Sets: UBH_RPGSettings::DebugArmorSets.

#include "Items/BH_ArmorVisualComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

#if !UE_BUILD_SHIPPING

DEFINE_LOG_CATEGORY_STATIC(LogBHArmorDebug, Log, All);

namespace BH_ArmorDebugCommands_Private
{
	static void Notify(const UWorld* World, const FString& Message)
	{
		UE_LOG(LogBHArmorDebug, Warning, TEXT("bh.Armor.Give: %s"), *Message);
		if (GEngine && World)
		{
			GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Yellow, FString::Printf(TEXT("bh.Armor.Give: %s"), *Message));
		}
	}

	static FAutoConsoleCommandWithWorldAndArgs CmdArmorGive(
		TEXT("bh.Armor.Give"),
		TEXT("bh.Armor.Give <Light|Medium|Heavy|Clear> - grants and equips the whole armor set for the local player (server RPC from a client)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() < 1)
			{
				Notify(World, TEXT("Usage: bh.Armor.Give <Light|Medium|Heavy|Clear>"));
				return;
			}
			const APlayerController* LocalPC = World ? World->GetFirstPlayerController() : nullptr;
			UBH_ArmorVisualComponent* ArmorComponent = LocalPC ? UBH_ArmorVisualComponent::Find(LocalPC->GetPawn()) : nullptr;
			if (!ArmorComponent)
			{
				Notify(World, TEXT("refused, no local player pawn with an armor component yet."));
				return;
			}
			ArmorComponent->ServerDebugArmorSet(Args[0]);
		}));
}

#endif // !UE_BUILD_SHIPPING
