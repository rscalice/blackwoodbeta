// Blackwood Hollow - Phase 11D consumable / Blight debug console commands
//
// Both go through the LOCAL player's pawn exactly like the bh.Loot.* commands (the world the command runs in decides the local player, so
// they work typed in the host / standalone window and in a client window; the server does the real work). Compiled out in Shipping.
//   bh.Consumable.Use <Sap|Incense>   use one Heartwood Sap / Warden's Incense from the local player's inventory, exactly like picking it on the
//                                     radial wheel (a client sends ServerUseConsumable, the host activates directly). Give yourself some first:
//                                     bh.Loot.Give Sap 3
//   bh.Blight.Set <0-100>             set the local player's Blight meter. 0 clears it, 100 saturates it (saturation damage, stagger, Blight Rot).

#include "Consumables/BH_ConsumableLibrary.h"
#include "Interaction/BH_InteractorComponent.h"
#include "Loot/BH_LootLibrary.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "NarrativeItem.h"

#if !UE_BUILD_SHIPPING

DEFINE_LOG_CATEGORY_STATIC(LogBHConsumableDebug, Log, All);

namespace BH_ConsumableDebugCommands_Private
{
	static void Notify(const UWorld* World, const TCHAR* CommandName, const FString& Reason)
	{
		UE_LOG(LogBHConsumableDebug, Warning, TEXT("%s: %s"), CommandName, *Reason);
		if (GEngine && World)
		{
			GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Yellow, FString::Printf(TEXT("%s: %s"), CommandName, *Reason));
		}
	}

	static APawn* GetLocalPawn(const UWorld* World, const TCHAR* CommandName)
	{
		const APlayerController* LocalPC = World ? World->GetFirstPlayerController() : nullptr;
		APawn* LocalPawn = LocalPC ? LocalPC->GetPawn() : nullptr;
		if (!LocalPawn)
		{
			Notify(World, CommandName, TEXT("refused, no local player pawn yet."));
		}
		return LocalPawn;
	}

	static FAutoConsoleCommandWithWorldAndArgs CmdConsumableUse(
		TEXT("bh.Consumable.Use"),
		TEXT("bh.Consumable.Use <Sap|Incense> - the local player uses one of that consumable (needs one in the inventory: bh.Loot.Give Sap 3)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() < 1)
			{
				Notify(World, TEXT("bh.Consumable.Use"), TEXT("Usage: bh.Consumable.Use <Sap|Incense>"));
				return;
			}
			const FString& Name = Args[0];
			const bool bKnownAlias = Name.Equals(TEXT("sap"), ESearchCase::IgnoreCase) || Name.Equals(TEXT("heartwood"), ESearchCase::IgnoreCase)
				|| Name.Equals(TEXT("incense"), ESearchCase::IgnoreCase);
			if (!bKnownAlias)
			{
				Notify(World, TEXT("bh.Consumable.Use"), TEXT("refused, use Sap or Incense."));
				return;
			}
			APawn* LocalPawn = GetLocalPawn(World, TEXT("bh.Consumable.Use"));
			if (!LocalPawn)
			{
				return;
			}
			const TSubclassOf<UNarrativeItem> ItemClass = UBH_LootLibrary::ResolveItemClass(Name);
			if (!ItemClass)
			{
				Notify(World, TEXT("bh.Consumable.Use"), TEXT("refused, the item Blueprint could not be loaded."));
				return;
			}
			if (!UBH_ConsumableLibrary::RequestUseConsumable(LocalPawn, ItemClass))
			{
				Notify(World, TEXT("bh.Consumable.Use"), TEXT("nothing sent, you have none of that item (bh.Loot.Give Sap 3) or the request was refused locally."));
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdBlightSet(
		TEXT("bh.Blight.Set"),
		TEXT("bh.Blight.Set <0-100> - sets the local player's Blight meter (100 saturates it)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() < 1 || !Args[0].IsNumeric())
			{
				Notify(World, TEXT("bh.Blight.Set"), TEXT("Usage: bh.Blight.Set <0-100>"));
				return;
			}
			const float Value = FCString::Atof(*Args[0]);
			if (Value < 0.f || Value > 100.f)
			{
				Notify(World, TEXT("bh.Blight.Set"), TEXT("refused, value must be 0..100."));
				return;
			}
			const APawn* LocalPawn = GetLocalPawn(World, TEXT("bh.Blight.Set"));
			if (UBH_InteractorComponent* Interactor = UBH_InteractorComponent::Find(LocalPawn))
			{
				Interactor->ServerDebugSetBlight(Value);
			}
			else if (LocalPawn)
			{
				Notify(World, TEXT("bh.Blight.Set"), TEXT("refused, the local pawn has no interactor component."));
			}
		}));
}

#endif // !UE_BUILD_SHIPPING
