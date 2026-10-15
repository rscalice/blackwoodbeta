// Blackwood Hollow - per-local-player hint presenter and Field Notes data (Phase 13E)
// Target: Unreal Engine 5.8 (C++)
//
// Local to each player (nothing here replicates). The server tells this machine WHICH hint to show (UBH_PlayerHintComponent); this
// subsystem decides WHEN: a hint waits for its delay, its prerequisites, a quiet moment (no boss intro, no open menu, the player alive,
// the gap since the last hint), then goes into the 13C notification queue as an EBH_NotificationKind::Hint toast. A hint counts as seen
// the moment its toast starts to show, and the seen ids are saved to a local SaveGame slot (one per local player).
//
// Field Notes (Quest Journal, built in 13G) read: GetFieldNotes() (seen hints in order), GetAllHints(), HasSeenHint(), OnFieldNotesChanged.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Containers/Ticker.h"
#include "UI/BH_NotificationTypes.h"
#include "BH_HintSubsystem.generated.h"

class APlayerController;
class UBH_HintData;
class UBH_HintSaveGame;
class UBH_NotificationSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBH_OnHintEvent, UBH_HintData*, Hint);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FBH_OnFieldNotesChanged);

UCLASS()
class BLACKWOODHOLLOWBETA_API UBH_HintSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	static UBH_HintSubsystem* ForController(const APlayerController* PC);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Local request (the server's ClientShowHint lands here). Ignored when the profile has seen the hint, or it is already waiting. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Hints")
	void RequestHint(FName HintId);

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Hints")
	bool HasSeenHint(FName HintId) const;

	/** Seen hint ids, in the order they were seen. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Hints")
	TArray<FName> GetSeenHintIds() const;

	// -- Field Notes API (for the 13G Quest Journal) -------------------------------------------

	/** Every hint asset, in SortOrder. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Hints|FieldNotes")
	TArray<UBH_HintData*> GetAllHints() const;

	/** The hints this player has seen, in SortOrder: the Field Notes list. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Hints|FieldNotes")
	TArray<UBH_HintData*> GetFieldNotes() const;

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Hints|FieldNotes")
	UBH_HintData* FindHint(FName HintId) const;

	/** Forget every seen hint and save (bh.Hint.Reset, and a future "Reset hints" setting). */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Hints|FieldNotes")
	void ResetSeenHints();

	/** A hint toast started to show (it is now seen). */
	UPROPERTY(BlueprintAssignable, Category = "BlackwoodHollow|Hints")
	FBH_OnHintEvent OnHintShown;

	/** The seen list changed (a hint was seen, or the list was reset). */
	UPROPERTY(BlueprintAssignable, Category = "BlackwoodHollow|Hints|FieldNotes")
	FBH_OnFieldNotesChanged OnFieldNotesChanged;

private:
	struct FWaiting
	{
		FName HintId;
		double ReadyTime = 0.0;
		int32 Sequence = 0;
	};

	bool Tick(float DeltaTime);
	void EnsureLoaded();
	bool IsQuietMoment() const;
	bool PrerequisitesMet(const UBH_HintData& Hint) const;
	void ShowHint(const UBH_HintData& Hint);
	void MarkSeen(FName HintId);
	void SaveSeen() const;
	FString GetSlotName() const;
	UBH_NotificationSubsystem* GetNotifications() const;
	APlayerController* GetPC() const;

	UFUNCTION()
	void HandleNotificationShown(const FBH_Notification& Notification);

	UFUNCTION()
	void HandleNotificationFinished();

	UPROPERTY(Transient)
	TObjectPtr<UBH_HintSaveGame> Save;

	FTSTicker::FDelegateHandle TickHandle;
	TArray<FWaiting> Waiting;
	FName InFlightId;
	double Clock = 0.0;
	double NotBefore = 0.0;
	double InFlightSince = 0.0;
	int32 NextSequence = 0;
	bool bBoundNotifications = false;
	bool bLoaded = false;
	bool bInFlightShown = false;
};
