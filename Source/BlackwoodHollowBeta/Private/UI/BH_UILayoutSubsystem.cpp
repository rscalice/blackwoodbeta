// Blackwood Hollow - Per-local-player UI layout owner (Phase 13B, implementation)

#include "UI/BH_UILayoutSubsystem.h"
#include "UI/BH_PrimaryLayoutWidget.h"
#include "Blueprint/UserWidget.h"
#include "CommonActivatableWidget.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Input/CommonUIActionRouterBase.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/World.h"
#include "TimerManager.h"

UBH_PrimaryLayoutWidget* UBH_UILayoutSubsystem::EnsureLayout(TSubclassOf<UBH_PrimaryLayoutWidget> LayoutClass)
{
	if (Layout)
	{
		return Layout;
	}
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	APlayerController* PC = LocalPlayer ? LocalPlayer->GetPlayerController(GetWorld()) : nullptr;
	if (!PC || !LayoutClass)
	{
		return nullptr;
	}
	Layout = CreateWidget<UBH_PrimaryLayoutWidget>(PC, LayoutClass);
	if (Layout)
	{
		Layout->AddToPlayerScreen(LayoutZOrder);
		OnLayoutReady.Broadcast();
	}
	return Layout;
}

UCommonActivatableWidget* UBH_UILayoutSubsystem::PushWidgetToLayer(FGameplayTag LayerTag, TSubclassOf<UCommonActivatableWidget> WidgetClass)
{
	if (!Layout)
	{
		UE_LOG(LogTemp, Warning, TEXT("BH_UILayoutSubsystem::PushWidgetToLayer: no layout yet (call EnsureLayout first)."));
		return nullptr;
	}
	return Layout->PushWidgetToLayer(LayerTag, WidgetClass);
}

void UBH_UILayoutSubsystem::AddHUDWidget(UUserWidget* HUDWidget)
{
	if (Layout)
	{
		Layout->AddHUDWidget(HUDWidget);
	}
}

bool UBH_UILayoutSubsystem::IsAnyMenuOpen() const
{
	return Layout && Layout->IsAnyMenuOpen();
}

void UBH_UILayoutSubsystem::NotifyMenuStateChanged()
{
	const bool bOpen = IsAnyMenuOpen();
	if (bOpen != bLastAnyMenuOpen)
	{
		bLastAnyMenuOpen = bOpen;
		OnMenuStateChanged.Broadcast(bOpen);
	}
	// Idempotent: also runs when the state did not change, because CommonUI re-applies its Menu config as the stack settles.
	ApplyInputForMenuState(bOpen);
}

void UBH_UILayoutSubsystem::RestoreGameInputNow() const
{
	const ULocalPlayer* LP = GetLocalPlayer();
	APlayerController* PC = LP ? LP->GetPlayerController(GetWorld()) : nullptr;
	if (!PC || !PC->IsLocalController())
	{
		return;
	}
	// CommonUI's action router keeps the last UI input config (Menu / NoCapture) and re-applies it, so a plain SetInputMode is
	// undone. Tell the router itself to go back to Game input (Lyra pattern), then fix cursor and focus.
	if (Layout)
	{
		if (UCommonUIActionRouterBase* Router = UCommonUIActionRouterBase::Get(*Layout))
		{
			Router->SetActiveUIInputConfig(FUIInputConfig(ECommonInputMode::Game, EMouseCaptureMode::CapturePermanently_IncludingInitialMouseDown, /*bHideCursorDuringViewportCapture*/ true));
		}
	}
	PC->SetInputMode(FInputModeGameOnly());
	PC->SetShowMouseCursor(false);
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
}

void UBH_UILayoutSubsystem::ApplyInputForMenuState(bool bAnyMenuOpen)
{
	if (bAnyMenuOpen)
	{
		return;
	}
	RestoreGameInputNow();

	// Once more next tick (after the stack transition), with the result logged.
	if (UWorld* World = GetWorld())
	{
		TWeakObjectPtr<UBH_UILayoutSubsystem> WeakThis(this);
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([WeakThis]()
		{
			UBH_UILayoutSubsystem* Self = WeakThis.Get();
			if (!Self || Self->IsAnyMenuOpen())
			{
				return;
			}
			Self->RestoreGameInputNow();
			const ULocalPlayer* LP = Self->GetLocalPlayer();
			const APlayerController* PC = LP ? LP->GetPlayerController(Self->GetWorld()) : nullptr;
			ECommonInputMode Mode = ECommonInputMode::All;
			if (Self->Layout)
			{
				if (const UCommonUIActionRouterBase* Router = UCommonUIActionRouterBase::Get(*Self->Layout))
				{
					Mode = Router->GetActiveInputMode();
				}
			}
			UE_LOG(LogTemp, Log, TEXT("[BH_Menu] restore: mode=%d cursor=%d"), static_cast<int32>(Mode), (PC && PC->bShowMouseCursor) ? 1 : 0);
		}));
	}
}
void UBH_UILayoutSubsystem::Deinitialize()
{
	if (Layout)
	{
		Layout->RemoveFromParent();
		Layout = nullptr;
	}
	Super::Deinitialize();
}
