// Blackwood Hollow - Per-local-player HUD owner (implementation)

#include "UI/BH_HUDSubsystem.h"
#include "UI/BH_HUDWidget.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Blueprint/UserWidget.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

bool UBH_HUDSubsystem::SetupHUD(APawn* Pawn, TSubclassOf<UBH_HUDWidget> MainHUDClass, TSubclassOf<UUserWidget> DebugHUDClass)
{
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	APlayerController* PC = LocalPlayer ? LocalPlayer->GetPlayerController(Pawn ? Pawn->GetWorld() : nullptr) : nullptr;
	if (!Pawn || !PC || Pawn->GetController() != PC)
	{
		return false;
	}

	if (!MainHUD && MainHUDClass)
	{
		MainHUD = CreateWidget<UBH_HUDWidget>(PC, MainHUDClass);
		if (MainHUD)
		{
			MainHUD->AddToViewport(0);
		}
	}
	if (!DebugHUD && DebugHUDClass)
	{
		DebugHUD = CreateWidget<UUserWidget>(PC, DebugHUDClass);
		if (DebugHUD)
		{
			DebugHUD->AddToViewport(1);
		}
	}

	// Bind after AddToViewport so NativeConstruct has already run.
	if (MainHUD)
	{
		UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Pawn);
		if (!ASC)
		{
			// Blueprint characters may carry the component without implementing IAbilitySystemInterface.
			ASC = Pawn->FindComponentByClass<UAbilitySystemComponent>();
		}
		MainHUD->InitializeHUD(ASC);
	}

	ApplyVisibility();
	return MainHUD != nullptr;
}

void UBH_HUDSubsystem::SetDebugHUDVisible(bool bVisible)
{
	bDebugVisible = bVisible;
	ApplyVisibility();
}

void UBH_HUDSubsystem::ApplyVisibility()
{
	// With no debug HUD there is nothing to switch to: keep the production HUD up.
	const bool bShowDebug = bDebugVisible && DebugHUD;
	if (MainHUD)
	{
		MainHUD->SetVisibility(bShowDebug ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	}
	if (DebugHUD)
	{
		DebugHUD->SetVisibility(bShowDebug ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void UBH_HUDSubsystem::Deinitialize()
{
	if (MainHUD)
	{
		MainHUD->RemoveFromParent();
		MainHUD = nullptr;
	}
	if (DebugHUD)
	{
		DebugHUD->RemoveFromParent();
		DebugHUD = nullptr;
	}
	Super::Deinitialize();
}

// ----------------------------------------------------------------------------
// Console: BH.HUD.ToggleDebug / BH.HUD.Debug 0|1 (every local player in the world)
// ----------------------------------------------------------------------------
namespace BH_HUDSubsystem_Private
{
	static void ForEachHUDSubsystem(UWorld* World, TFunctionRef<void(UBH_HUDSubsystem&)> Fn)
	{
		const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		if (!GameInstance)
		{
			return;
		}
		for (ULocalPlayer* LocalPlayer : GameInstance->GetLocalPlayers())
		{
			if (UBH_HUDSubsystem* HUD = LocalPlayer ? LocalPlayer->GetSubsystem<UBH_HUDSubsystem>() : nullptr)
			{
				Fn(*HUD);
			}
		}
	}

	static FAutoConsoleCommandWithWorld CmdToggleDebugHUD(
		TEXT("BH.HUD.ToggleDebug"),
		TEXT("Switch between the production HUD (WBP_HUD_Main) and the combat debug HUD (W_BH_DebugHUD)."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			ForEachHUDSubsystem(World, [](UBH_HUDSubsystem& HUD) { HUD.ToggleDebugHUD(); });
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdSetDebugHUD(
		TEXT("BH.HUD.Debug"),
		TEXT("BH.HUD.Debug 1 shows the combat debug HUD, 0 shows the production HUD."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const bool bShow = Args.Num() > 0 ? FCString::Atoi(*Args[0]) != 0 : true;
			ForEachHUDSubsystem(World, [bShow](UBH_HUDSubsystem& HUD) { HUD.SetDebugHUDVisible(bShow); });
		}));
}
