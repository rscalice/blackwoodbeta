// Blackwood Hollow - Activatable menu widget base (Phase 13B, implementation)

#include "UI/BH_MenuActivatableWidget.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"

TOptional<FUIInputConfig> UBH_MenuActivatableWidget::GetDesiredInputConfig() const
{
	return FUIInputConfig(ECommonInputMode::Menu, EMouseCaptureMode::NoCapture);
}

void UBH_MenuActivatableWidget::NativeOnActivated()
{
	Super::NativeOnActivated();
	AddMenuContext();
}

void UBH_MenuActivatableWidget::NativeOnDeactivated()
{
	RemoveMenuContext();
	Super::NativeOnDeactivated();
}

void UBH_MenuActivatableWidget::NativeDestruct()
{
	// Safety net: a widget destroyed while still active (level change, forced removal) must not leave the context behind.
	RemoveMenuContext();
	Super::NativeDestruct();
}

void UBH_MenuActivatableWidget::AddMenuContext()
{
	if (bContextAdded || !MenuMappingContext)
	{
		return;
	}
	const APlayerController* PC = GetOwningPlayer();
	const ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
	UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return;
	}
	Subsystem->AddMappingContext(MenuMappingContext, MenuMappingPriority);
	ContextSubsystem = Subsystem;
	bContextAdded = true;
}

void UBH_MenuActivatableWidget::RemoveMenuContext()
{
	if (!bContextAdded)
	{
		return;
	}
	bContextAdded = false;
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ContextSubsystem.Get())
	{
		if (MenuMappingContext)
		{
			Subsystem->RemoveMappingContext(MenuMappingContext);
		}
	}
	ContextSubsystem.Reset();
}
