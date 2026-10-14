// Blackwood Hollow - Per-local-player UI layout owner (Phase 13B, implementation)

#include "UI/BH_UILayoutSubsystem.h"
#include "UI/BH_PrimaryLayoutWidget.h"
#include "Blueprint/UserWidget.h"
#include "CommonActivatableWidget.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"

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
