// Blackwood Hollow - Primary UI layout (Phase 13B, implementation)

#include "UI/BH_PrimaryLayoutWidget.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "CommonActivatableWidget.h"
#include "Widgets/CommonActivatableWidgetContainer.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Blueprint/UserWidget.h"
#include "Engine/LocalPlayer.h"
#include "UI/BH_UILayoutSubsystem.h"

UCommonActivatableWidgetStack* UBH_PrimaryLayoutWidget::GetLayerStack(FGameplayTag LayerTag) const
{
	if (LayerTag == TAG_UI_Layer_GameMenu)
	{
		return GameMenuStack;
	}
	if (LayerTag == TAG_UI_Layer_Menu)
	{
		return MenuStack;
	}
	if (LayerTag == TAG_UI_Layer_Modal)
	{
		return ModalStack;
	}
	return nullptr;
}

UCommonActivatableWidget* UBH_PrimaryLayoutWidget::PushWidgetToLayer(FGameplayTag LayerTag, TSubclassOf<UCommonActivatableWidget> WidgetClass)
{
	if (!WidgetClass)
	{
		return nullptr;
	}
	if (LayerTag == TAG_UI_Layer_Game)
	{
		UE_LOG(LogTemp, Warning, TEXT("BH_PrimaryLayout: UI.Layer.Game is the non-activatable HUD layer; use AddHUDWidget (not pushing %s)."), *GetNameSafe(WidgetClass));
		return nullptr;
	}
	UCommonActivatableWidgetStack* Stack = GetLayerStack(LayerTag);
	if (!Stack)
	{
		UE_LOG(LogTemp, Warning, TEXT("BH_PrimaryLayout: unknown or unbound layer '%s' (not pushing %s)."), *LayerTag.ToString(), *GetNameSafe(WidgetClass));
		return nullptr;
	}
	return Stack->AddWidget(WidgetClass);
}

void UBH_PrimaryLayoutWidget::AddHUDWidget(UUserWidget* HUDWidget)
{
	if (!HUDWidget || !GameLayer || HUDWidget->GetParent() == GameLayer)
	{
		return;
	}
	if (UOverlaySlot* OverlaySlot = GameLayer->AddChildToOverlay(HUDWidget))
	{
		OverlaySlot->SetHorizontalAlignment(HAlign_Fill);
		OverlaySlot->SetVerticalAlignment(VAlign_Fill);
	}
}

void UBH_PrimaryLayoutWidget::RemoveHUDWidget(UUserWidget* HUDWidget)
{
	if (HUDWidget && GameLayer && HUDWidget->GetParent() == GameLayer)
	{
		GameLayer->RemoveChild(HUDWidget);
	}
}

void UBH_PrimaryLayoutWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	UCommonActivatableWidgetStack* Stacks[] = { GameMenuStack, MenuStack, ModalStack };
	for (UCommonActivatableWidgetStack* Stack : Stacks)
	{
		if (Stack)
		{
			Stack->OnDisplayedWidgetChanged().AddUObject(this, &UBH_PrimaryLayoutWidget::HandleDisplayedWidgetChanged);
		}
	}
}

void UBH_PrimaryLayoutWidget::NativeDestruct()
{
	UCommonActivatableWidgetStack* Stacks[] = { GameMenuStack, MenuStack, ModalStack };
	for (UCommonActivatableWidgetStack* Stack : Stacks)
	{
		if (Stack)
		{
			Stack->OnDisplayedWidgetChanged().RemoveAll(this);
		}
	}
	Super::NativeDestruct();
}

void UBH_PrimaryLayoutWidget::HandleDisplayedWidgetChanged(UCommonActivatableWidget* DisplayedWidget)
{
	if (const ULocalPlayer* LP = GetOwningLocalPlayer())
	{
		if (UBH_UILayoutSubsystem* Layout = LP->GetSubsystem<UBH_UILayoutSubsystem>())
		{
			Layout->NotifyMenuStateChanged();
		}
	}
}

bool UBH_PrimaryLayoutWidget::IsAnyMenuOpen() const
{
	// Count only activated widgets: GetActiveWidget() still returns a just-closed widget until the stack finishes its transition.
	const UCommonActivatableWidgetStack* Stacks[] = { GameMenuStack, MenuStack, ModalStack };
	for (const UCommonActivatableWidgetStack* Stack : Stacks)
	{
		if (!Stack) { continue; }
		for (const UCommonActivatableWidget* Widget : Stack->GetWidgetList())
		{
			if (Widget && Widget->IsActivated())
			{
				return true;
			}
		}
	}
	return false;
}