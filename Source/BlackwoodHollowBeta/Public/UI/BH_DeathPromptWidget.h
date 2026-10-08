// Blackwood Hollow - death / revive prompt widget (Phase 10B part 2)
// Target: Unreal Engine 5.8 (C++), UMG
//
// Parent of WBP_DeathPrompt. Widget Blueprints bind by widget NAME (all optional):
//   Txt_Prompt        UTextBlock    the main line: "Waiting for party to leave combat" / "You have fallen" / "Waiting for a party member to revive you"
//   Btn_Respawn       UButton       "Respawn at <Hub>" (clicking it = ChooseRespawn); visible while the revive window is open
//   Txt_RespawnLabel  UTextBlock    the label of Btn_Respawn ("Respawn at Port Vanguard")
//   Btn_Wait          UButton       "Wait for revive" (clicking it = ChooseWait); visible while the player has not chosen yet
//   Txt_WaitLabel     UTextBlock    the label of Btn_Wait
//   Bar_Revive        UProgressBar  revive progress 0..1 (shown while a revive is in progress, for the downed player AND for the reviver)
//   Txt_ReviveStatus  UTextBlock    "Being revived..." (downed player) / "Reviving <Name>..." (reviver)
//   Txt_RevivePrompt  UTextBlock    "Hold to revive" while a downed party member is within revive range (the reviver's hint)
//
// The widget finds the owning player's pawn by itself (the pawn may not exist yet), retrying on a short timer, then listens to its
// UBH_PlayerDeathComponent. While the player is alive and nothing is going on it collapses itself (bHideWhenIdle), so it can stay
// in WBP_HUD_Main permanently. While the revive window is open it shows the mouse cursor (bManageMouseCursor) so the buttons can
// be clicked, and restores game-only input afterwards. Blueprint events let the WBP animate / restyle.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Engine/TimerHandle.h"
#include "Player/BH_PlayerDeathComponent.h"
#include "BH_DeathPromptWidget.generated.h"

class APawn;
class UButton;
class UProgressBar;
class UTextBlock;

UCLASS(Abstract, Blueprintable)
class BLACKWOODHOLLOWBETA_API UBH_DeathPromptWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// -- Texts (FText so they can be localised / restyled per WBP) ---------------------------------

	/** Dead, the party is still fighting (or the body is still settling). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|DeathPrompt|Text")
	FText WaitingForPartyText = NSLOCTEXT("BlackwoodHollow", "DeathWaitingForParty", "Waiting for party to leave combat");

	/** The revive window is open and the player has not chosen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|DeathPrompt|Text")
	FText ChoicePromptText = NSLOCTEXT("BlackwoodHollow", "DeathChoicePrompt", "You have fallen");

	/** The player chose to wait. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|DeathPrompt|Text")
	FText WaitingForReviveText = NSLOCTEXT("BlackwoodHollow", "DeathWaitingForRevive", "Waiting for a party member to revive you");

	/** Format of the respawn button; {Hub} = the hub label ("Port Vanguard"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|DeathPrompt|Text")
	FText RespawnLabelFormat = NSLOCTEXT("BlackwoodHollow", "DeathRespawnFormat", "Respawn at {Hub}");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|DeathPrompt|Text")
	FText WaitLabel = NSLOCTEXT("BlackwoodHollow", "DeathWaitLabel", "Wait for revive");

	/** Downed player: somebody is reviving you. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|DeathPrompt|Text")
	FText BeingRevivedText = NSLOCTEXT("BlackwoodHollow", "DeathBeingRevived", "Being revived...");

	/** Reviver: format of the status line; {Name} = the downed player's name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|DeathPrompt|Text")
	FText RevivingFormat = NSLOCTEXT("BlackwoodHollow", "DeathRevivingFormat", "Reviving {Name}...");

	/** Reviver: shown when a downed party member is within range. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|DeathPrompt|Text")
	FText RevivePromptText = NSLOCTEXT("BlackwoodHollow", "DeathRevivePrompt", "Hold to revive");

	// -- Behaviour -----------------------------------------------------------------------------------

	/** Collapse the widget while the player is alive and no revive is in progress / possible. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|DeathPrompt")
	bool bHideWhenIdle = true;

	/** Show the mouse cursor (game + UI input) while the revive window is open so the buttons can be clicked. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|DeathPrompt")
	bool bManageMouseCursor = true;

	/** Seconds between attempts to find the owning pawn, and between revive-prompt range checks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|DeathPrompt", meta = (ClampMin = "0.05", ForceUnits = "s"))
	float PollInterval = 0.25f;

	// -- Choices (bind to the buttons; also callable from the WBP) ---------------------------------------

	/** Respawn at the nearest hub (server RPC through the death component). */
	UFUNCTION(BlueprintCallable, Category = "BH|DeathPrompt")
	void ChooseRespawn();

	/** Stay down and wait for a party member to revive. */
	UFUNCTION(BlueprintCallable, Category = "BH|DeathPrompt")
	void ChooseWait();

	// -- State ---------------------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "BH|DeathPrompt")
	EBH_DeathPhase GetDeathPhase() const;

	/** "Port Vanguard" (empty until the window opens). */
	UFUNCTION(BlueprintPure, Category = "BH|DeathPrompt")
	FString GetRespawnHubLabel() const;

	/** The local player's revive progress, 0..1 (as the downed player or as the reviver). */
	UFUNCTION(BlueprintPure, Category = "BH|DeathPrompt")
	float GetReviveFraction() const { return ReviveFraction; }

	// -- Events ----------------------------------------------------------------------------------------

	/** The local player's death phase changed (also fires once on bind). */
	UFUNCTION(BlueprintImplementableEvent, Category = "BH|DeathPrompt")
	void OnDeathPhaseChanged(EBH_DeathPhase NewPhase, EBH_DeathPhase OldPhase);

	/** Revive progress changed. bIsBeingRevived = you are the downed player; bIsReviving = you are the reviver. Both false = stopped. */
	UFUNCTION(BlueprintImplementableEvent, Category = "BH|DeathPrompt")
	void OnReviveProgress(float Fraction, bool bIsBeingRevived, bool bIsReviving);

	/** A downed party member entered / left your revive range (Candidate is null on leave). */
	UFUNCTION(BlueprintImplementableEvent, Category = "BH|DeathPrompt")
	void OnRevivePromptChanged(bool bShow, APawn* Candidate);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(BlueprintReadOnly, Category = "BH|DeathPrompt", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Prompt;

	UPROPERTY(BlueprintReadOnly, Category = "BH|DeathPrompt", meta = (BindWidgetOptional))
	TObjectPtr<UButton> Btn_Respawn;

	UPROPERTY(BlueprintReadOnly, Category = "BH|DeathPrompt", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_RespawnLabel;

	UPROPERTY(BlueprintReadOnly, Category = "BH|DeathPrompt", meta = (BindWidgetOptional))
	TObjectPtr<UButton> Btn_Wait;

	UPROPERTY(BlueprintReadOnly, Category = "BH|DeathPrompt", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_WaitLabel;

	UPROPERTY(BlueprintReadOnly, Category = "BH|DeathPrompt", meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> Bar_Revive;

	UPROPERTY(BlueprintReadOnly, Category = "BH|DeathPrompt", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_ReviveStatus;

	UPROPERTY(BlueprintReadOnly, Category = "BH|DeathPrompt", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_RevivePrompt;

private:
	void Poll();
	void TryBind();
	void Bind(UBH_PlayerDeathComponent* Comp);
	void Unbind();
	void Refresh();
	void SetCursorForChoice(bool bChoosing);

	UFUNCTION()
	void HandlePhaseChanged(EBH_DeathPhase NewPhase, EBH_DeathPhase OldPhase);

	UFUNCTION()
	void HandleReviveProgress(float Fraction, bool bIsDeadPlayerView, APawn* OtherPlayer);

	TWeakObjectPtr<UBH_PlayerDeathComponent> BoundComponent;
	TWeakObjectPtr<APawn> RevivePromptCandidate;
	FTimerHandle PollTimer;

	float ReviveFraction = 0.f;
	bool bBeingRevived = false;
	bool bReviving = false;
	bool bShowingRevivePrompt = false;
	bool bCursorShown = false;
	FText RevivingName;
};
