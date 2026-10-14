// Blackwood Hollow - quest tracker slot under the compass (Phase 13C)
// Target: Unreal Engine 5.8 (C++), UMG
//
// Parent of WBP_QuestTracker. Shows the current beat's objective text (DT_BH_Beats, via UBH_BeatTrackerComponent on the game state).
// Phase 14 fills the same slot from quests later. Local and cosmetic: the beat is replicated, the text is looked up on each machine.
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

	void TryBind();
	void Unbind();
	void Refresh();

	TWeakObjectPtr<UBH_BeatTrackerComponent> BoundTracker;
	FText ObjectiveText;
	float RetryTimer = 0.f;
	bool bHadText = false;
};
