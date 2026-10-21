// Blackwood Hollow - quest tracker slot under the compass (Phase 13C)
// Target: Unreal Engine 5.8 (C++), UMG
//
// Parent of WBP_QuestTracker. Shows the current beat's objective text (DT_BH_Beats, via UBH_BeatTrackerComponent on the game state).
// Phase 14A-1: when UBH_QuestDirector (game state) has a current quest objective the slot shows it instead: the header becomes the quest title and
// the objective text is up to 3 lines with "(n/m)" counts. With no quest objective it falls back to the beat text and HeaderText (14A-3 retires that).
// Local and cosmetic: the quest / beat state is replicated, the text is looked up on each machine.
// Named widgets (all optional):
//   Panel_Root    UWidget    whole slot; collapsed while there is no objective
//   Txt_Header    UTextBlock "OBJECTIVE" (TS_Header)
//   Txt_Objective UTextBlock the objective line (TS_BodyLarge)

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "BH_QuestTrackerWidget.generated.h"

class UTextBlock;
class UBH_BeatTrackerComponent;
class UBH_QuestDirector;

UCLASS(Abstract, Blueprintable)
class BLACKWOODHOLLOWBETA_API UBH_QuestTrackerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|QuestTracker")
	FText HeaderText = NSLOCTEXT("BlackwoodHollow", "QuestTrackerHeader", "Objective");

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|QuestTracker")
	FText GetObjectiveText() const { return ObjectiveText; }

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** The objective text changed (play a short highlight). bFirst is true for the first text after binding. */
	UFUNCTION(BlueprintImplementableEvent, Category = "BlackwoodHollow|QuestTracker", meta = (DisplayName = "On Objective Changed"))
	void K2_OnObjectiveChanged(const FText& NewText, bool bFirst);

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional))
	TObjectPtr<UWidget> Panel_Root;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Header;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Objective;

private:
	UFUNCTION()
	void HandleObjectiveChanged();

	UFUNCTION()
	void HandleBeatChanged(FGameplayTag NewBeat, FGameplayTag OldBeat);

	UFUNCTION()
	void HandleQuestObjectiveChanged();

	void TryBind();
	void Unbind();
	void Refresh();

	TWeakObjectPtr<UBH_BeatTrackerComponent> BoundTracker;
	TWeakObjectPtr<UBH_QuestDirector> BoundDirector;
	FText ObjectiveText;
	float RetryTimer = 0.f;
	bool bHadText = false;
};
