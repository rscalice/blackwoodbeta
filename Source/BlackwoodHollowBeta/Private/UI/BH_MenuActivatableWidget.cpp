// Blackwood Hollow - Activatable menu widget base (Phase 13B, implementation)

#include "UI/BH_MenuActivatableWidget.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "UI/BH_UILayoutSubsystem.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Input/CommonUIActionRouterBase.h"

namespace
{
	/** Pre-processor: sees keys before Slate navigation / CommonUI, so Tab and gamepad B / View / Start always reach the menu. */
	class FBH_MenuCloseProcessor : public IInputProcessor
	{
	public:
		explicit FBH_MenuCloseProcessor(UBH_MenuActivatableWidget* InOwner) : Owner(InOwner) {}
		virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}
		virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
		{
			UBH_MenuActivatableWidget* Menu = Owner.Get();
			return Menu && Menu->TryCloseFromKey(InKeyEvent);
		}
		virtual const TCHAR* GetDebugName() const override { return TEXT("BH_MenuClose"); }
	private:
		TWeakObjectPtr<UBH_MenuActivatableWidget> Owner;
	};
}

UBH_MenuActivatableWidget::UBH_MenuActivatableWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	CloseKeys = { EKeys::Tab, EKeys::Gamepad_FaceButton_Right, EKeys::Gamepad_Special_Left, EKeys::Gamepad_Special_Right };
}

bool UBH_MenuActivatableWidget::TryCloseFromKey(const FKeyEvent& KeyEvent)
{
	if (KeyEvent.IsRepeat() || !IsActivated() || !CloseKeys.Contains(KeyEvent.GetKey()))
	{
		return false;
	}
	// PIE runs several players in one process: only the menu in the active window reacts.
	const TSharedPtr<SWindow> MyWindow = FSlateApplication::Get().FindWidgetWindow(TakeWidget());
	if (MyWindow.IsValid() && FSlateApplication::Get().GetActiveTopLevelWindow() != MyWindow)
	{
		return false;
	}
	UE_LOG(LogTemp, Log, TEXT("[BH_Menu] %s closed by key %s"), *GetName(), *KeyEvent.GetKey().ToString());
	DeactivateWidget();
	return true;
}

void UBH_MenuActivatableWidget::RegisterCloseProcessor()
{
	if (!CloseProcessor.IsValid() && FSlateApplication::IsInitialized())
	{
		CloseProcessor = MakeShared<FBH_MenuCloseProcessor>(this);
		FSlateApplication::Get().RegisterInputPreProcessor(CloseProcessor);
	}
}

void UBH_MenuActivatableWidget::UnregisterCloseProcessor()
{
	if (CloseProcessor.IsValid())
	{
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().UnregisterInputPreProcessor(CloseProcessor);
		}
		CloseProcessor.Reset();
	}
}

void UBH_MenuActivatableWidget::NotifyLayoutMenuState() const
{
	if (const APlayerController* PC = GetOwningPlayer())
	{
		if (const ULocalPlayer* LP = PC->GetLocalPlayer())
		{
			if (UBH_UILayoutSubsystem* Layout = LP->GetSubsystem<UBH_UILayoutSubsystem>())
			{
				Layout->NotifyMenuStateChanged();
			}
		}
	}
}

bool UBH_MenuActivatableWidget::ConsumeNotPausedCue()
{
	if (!bShowNotPausedCue)
	{
		return false;
	}
	const APlayerController* PC = GetOwningPlayer();
	const ULocalPlayer* LP = PC ? PC->GetLocalPlayer() : nullptr;
	UBH_UILayoutSubsystem* Layout = LP ? LP->GetSubsystem<UBH_UILayoutSubsystem>() : nullptr;
	return Layout && Layout->ConsumeFirstOpenCue();
}

TOptional<FUIInputConfig> UBH_MenuActivatableWidget::GetDesiredInputConfig() const
{
	return FUIInputConfig(ECommonInputMode::Menu, EMouseCaptureMode::NoCapture);
}

void UBH_MenuActivatableWidget::NativeOnActivated()
{
	Super::NativeOnActivated();
	AddMenuContext();
	RegisterCloseProcessor();
	NotifyLayoutMenuState();
}

void UBH_MenuActivatableWidget::NativeOnDeactivated()
{
	RemoveMenuContext();
	UnregisterCloseProcessor();
	Super::NativeOnDeactivated();

	// The layout subsystem decides whether any menu is still open and, when none is, puts the player back into game input.
	NotifyLayoutMenuState();
}
void UBH_MenuActivatableWidget::NativeDestruct()
{
	// Safety net: a widget destroyed while still active (level change, forced removal) must not leave the context behind.
	RemoveMenuContext();
	UnregisterCloseProcessor();
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
