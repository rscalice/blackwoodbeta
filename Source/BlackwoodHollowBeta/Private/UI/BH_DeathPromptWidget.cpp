// Blackwood Hollow - death / revive prompt widget (implementation)

#include "UI/BH_DeathPromptWidget.h"
#include "Components/Button.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "TimerManager.h"

void UBH_DeathPromptWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (Btn_Respawn)
	{
		Btn_Respawn->OnClicked.AddDynamic(this, &UBH_DeathPromptWidget::ChooseRespawn);
	}
	if (Btn_Wait)
	{
		Btn_Wait->OnClicked.AddDynamic(this, &UBH_DeathPromptWidget::ChooseWait);
	}
}

void UBH_DeathPromptWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (IsDesignTime())
	{
		return;
	}

	ReviveFraction = 0.f;
	bBeingRevived = false;
	bReviving = false;
	bShowingRevivePrompt = false;
	Refresh(); // start collapsed / empty

	TryBind();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(PollTimer, this, &UBH_DeathPromptWidget::Poll, FMath::Max(0.05f, PollInterval), true);
	}
}

void UBH_DeathPromptWidget::NativeDestruct()
{
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PollTimer);
	}
	Unbind();
	Super::NativeDestruct();
}

// ----------------------------------------------------------------------------
// Binding
// ----------------------------------------------------------------------------

void UBH_DeathPromptWidget::Poll()
{
	TryBind();

	// The reviver's hint: a downed party member is within range.
	const UBH_PlayerDeathComponent* Comp = BoundComponent.Get();
	APawn* Candidate = Comp ? Comp->FindReviveCandidate() : nullptr;
	if (Candidate != RevivePromptCandidate.Get())
	{
		RevivePromptCandidate = Candidate;
		bShowingRevivePrompt = Candidate != nullptr;
		OnRevivePromptChanged(bShowingRevivePrompt, Candidate);
		Refresh();
	}
}

void UBH_DeathPromptWidget::TryBind()
{
	// The pawn may not exist yet (or may be replaced): look it up every time rather than caching it.
	const APlayerController* PC = GetOwningPlayer();
	UBH_PlayerDeathComponent* Found = UBH_PlayerDeathComponent::Find(PC ? PC->GetPawn() : nullptr);
	if (Found != BoundComponent.Get())
	{
		if (Found)
		{
			Bind(Found);
		}
		else
		{
			Unbind();
		}
	}
}

void UBH_DeathPromptWidget::Bind(UBH_PlayerDeathComponent* Comp)
{
	Unbind();
	if (!Comp)
	{
		return;
	}
	BoundComponent = Comp;
	Comp->OnDeathPhaseChanged.AddDynamic(this, &UBH_DeathPromptWidget::HandlePhaseChanged);
	Comp->OnReviveProgressChanged.AddDynamic(this, &UBH_DeathPromptWidget::HandleReviveProgress);

	// Initialise from the current state (the pawn may already be down); revive progress follows with the next update.
	HandlePhaseChanged(Comp->GetPhase(), EBH_DeathPhase::Alive);
}

void UBH_DeathPromptWidget::Unbind()
{
	if (UBH_PlayerDeathComponent* Comp = BoundComponent.Get())
	{
		Comp->OnDeathPhaseChanged.RemoveDynamic(this, &UBH_DeathPromptWidget::HandlePhaseChanged);
		Comp->OnReviveProgressChanged.RemoveDynamic(this, &UBH_DeathPromptWidget::HandleReviveProgress);
	}
	BoundComponent.Reset();
	RevivePromptCandidate.Reset();
	ReviveFraction = 0.f;
	bBeingRevived = false;
	bReviving = false;
	bShowingRevivePrompt = false;
	SetCursorForChoice(false);
	Refresh();
}

// ----------------------------------------------------------------------------
// Events
// ----------------------------------------------------------------------------

void UBH_DeathPromptWidget::HandlePhaseChanged(EBH_DeathPhase NewPhase, EBH_DeathPhase OldPhase)
{
	if (NewPhase == EBH_DeathPhase::Alive)
	{
		ReviveFraction = 0.f;
		bBeingRevived = false;
	}
	const UBH_PlayerDeathComponent* Comp = BoundComponent.Get();
	SetCursorForChoice(Comp && Comp->IsWindowOpen());
	OnDeathPhaseChanged(NewPhase, OldPhase);
	Refresh();
}

void UBH_DeathPromptWidget::HandleReviveProgress(float Fraction, bool bIsDeadPlayerView, APawn* OtherPlayer)
{
	ReviveFraction = Fraction;
	bBeingRevived = bIsDeadPlayerView && OtherPlayer != nullptr;
	bReviving = !bIsDeadPlayerView && OtherPlayer != nullptr;

	RevivingName = FText::GetEmpty();
	if (bReviving)
	{
		const APlayerState* OtherState = OtherPlayer->GetPlayerState();
		RevivingName = OtherState ? FText::FromString(OtherState->GetPlayerName()) : FText::GetEmpty();
	}

	OnReviveProgress(Fraction, bBeingRevived, bReviving);
	Refresh();
}

// ----------------------------------------------------------------------------
// Choices
// ----------------------------------------------------------------------------

void UBH_DeathPromptWidget::ChooseRespawn()
{
	if (UBH_PlayerDeathComponent* Comp = BoundComponent.Get())
	{
		Comp->RequestRespawnAtHub();
	}
}

void UBH_DeathPromptWidget::ChooseWait()
{
	if (UBH_PlayerDeathComponent* Comp = BoundComponent.Get())
	{
		Comp->RequestWaitForRevive();
	}
}

EBH_DeathPhase UBH_DeathPromptWidget::GetDeathPhase() const
{
	const UBH_PlayerDeathComponent* Comp = BoundComponent.Get();
	return Comp ? Comp->GetPhase() : EBH_DeathPhase::Alive;
}

FString UBH_DeathPromptWidget::GetRespawnHubLabel() const
{
	const UBH_PlayerDeathComponent* Comp = BoundComponent.Get();
	return Comp ? Comp->GetRespawnHubLabel() : FString();
}

// ----------------------------------------------------------------------------
// Presentation
// ----------------------------------------------------------------------------

void UBH_DeathPromptWidget::Refresh()
{
	const EBH_DeathPhase Phase = GetDeathPhase();
	const bool bDown = Phase != EBH_DeathPhase::Alive;
	const bool bProgressActive = bBeingRevived || bReviving;

	FText PromptText;
	bool bShowRespawn = false;
	bool bShowWait = false;
	switch (Phase)
	{
	case EBH_DeathPhase::Dead:
		PromptText = WaitingForPartyText;
		break;
	case EBH_DeathPhase::AwaitingChoice:
		PromptText = ChoicePromptText;
		bShowRespawn = true;
		bShowWait = true;
		break;
	case EBH_DeathPhase::WaitingForRevive:
		PromptText = WaitingForReviveText;
		bShowRespawn = true; // a hub respawn stays available
		break;
	default:
		break;
	}

	if (Txt_Prompt)
	{
		Txt_Prompt->SetText(PromptText);
		Txt_Prompt->SetVisibility(bDown ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (Btn_Respawn)
	{
		Btn_Respawn->SetVisibility(bShowRespawn ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (Txt_RespawnLabel)
	{
		Txt_RespawnLabel->SetText(FText::FormatNamed(RespawnLabelFormat, TEXT("Hub"), FText::FromString(GetRespawnHubLabel())));
	}
	if (Btn_Wait)
	{
		Btn_Wait->SetVisibility(bShowWait ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (Txt_WaitLabel)
	{
		Txt_WaitLabel->SetText(WaitLabel);
	}
	if (Bar_Revive)
	{
		Bar_Revive->SetPercent(ReviveFraction);
		Bar_Revive->SetVisibility(bProgressActive ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (Txt_ReviveStatus)
	{
		Txt_ReviveStatus->SetText(bReviving ? FText::FormatNamed(RevivingFormat, TEXT("Name"), RevivingName) : BeingRevivedText);
		Txt_ReviveStatus->SetVisibility(bProgressActive ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (Txt_RevivePrompt)
	{
		Txt_RevivePrompt->SetText(RevivePromptText);
		Txt_RevivePrompt->SetVisibility((bShowingRevivePrompt && !bProgressActive && !bDown) ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	if (bHideWhenIdle)
	{
		const bool bAnythingToShow = bDown || bProgressActive || bShowingRevivePrompt;
		SetVisibility(bAnythingToShow ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void UBH_DeathPromptWidget::SetCursorForChoice(bool bChoosing)
{
	if (!bManageMouseCursor || bChoosing == bCursorShown)
	{
		return;
	}
	APlayerController* PC = GetOwningPlayer();
	if (!PC)
	{
		return;
	}
	bCursorShown = bChoosing;
	if (bChoosing)
	{
		PC->SetShowMouseCursor(true);
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(Mode);
	}
	else
	{
		PC->SetShowMouseCursor(false);
		PC->SetInputMode(FInputModeGameOnly());
	}
}
