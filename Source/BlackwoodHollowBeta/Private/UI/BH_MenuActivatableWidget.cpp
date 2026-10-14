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

void UBH_MenuActivatableWidget::RestoreGameInputIfNoMenu(APlayerController* PC)
{
	if (!PC || !PC->IsLocalController())
	{
		return;
	}
	if (const ULocalPlayer* LP = PC->GetLocalPlayer())
	{
		if (const UBH_UILayoutSubsystem* Layout = LP->GetSubsystem<UBH_UILayoutSubsystem>())
		{
			if (Layout->IsAnyMenuOpen())
			{
				return;
			}
		}
	}
	PC->SetInputMode(FInputModeGameOnly());
	PC->bShowMouseCursor = false;
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
}

void UBH_MenuActivatableWidget::NativeOnDeactivated()
{
	RemoveMenuContext();
	UnregisterCloseProcessor();
	Super::NativeOnDeactivated();

	// CommonUI leaves the Menu input mode behind when the last menu closes: put the player back in game input.
	// Done now and once more next tick (after the activatable stack has settled), only if no other menu is open.
	if (APlayerController* PC = GetOwningPlayer())
	{
		RestoreGameInputIfNoMenu(PC);
		if (UWorld* World = GetWorld())
		{
			TWeakObjectPtr<APlayerController> WeakPC(PC);
			World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([WeakPC]()
			{
				RestoreGameInputIfNoMenu(WeakPC.Get());
			}));
		}
	}
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
