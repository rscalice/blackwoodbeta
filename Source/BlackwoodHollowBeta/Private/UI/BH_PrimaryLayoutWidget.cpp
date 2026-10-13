// Blackwood Hollow - Primary UI layout (Phase 13B, implementation)

#include "UI/BH_PrimaryLayoutWidget.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "CommonActivatableWidget.h"
#include "Widgets/CommonActivatableWidgetContainer.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Blueprint/UserWidget.h"

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

bool UBH_PrimaryLayoutWidget::IsAnyMenuOpen() const
{
	const UCommonActivatableWidgetStack* Stacks[] = { GameMenuStack, MenuStack, ModalStack };
	for (const UCommonActivatableWidgetStack* Stack : Stacks)
	{
		if (Stack && Stack->GetActiveWidget())
		{
			return true;
		}
	}
	return false;
}
