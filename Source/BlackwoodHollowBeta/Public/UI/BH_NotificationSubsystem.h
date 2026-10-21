// Blackwood Hollow - per-local-player notification queue (Phase 13C)
// Target: Unreal Engine 5.8 (C++), UMG
//
// ONE toast at a time, local to each player (nothing here replicates). Priority death > level-up > quest > discovery > loot.
// Loot toasts batch: pickups inside LootBatchWindow, and while a loot toast is still absorbing, merge into one toast ("+3 Coral Shard").
// A new Death notification replaces whatever is showing and drops everything else. While suppressed (the local player is down) only
// death notifications are accepted.
//
// Fed by the existing client events: UBH_InteractorComponent (pickups, "Inventory full", notices), UBH_HUDSubsystem (level-up),
// ABH_PlayerState (refused consumables). Shown by UBH_NotificationQueueWidget, which binds the four delegates below.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Containers/Ticker.h"
#include "Progression/BH_LevelUpInfo.h"
#include "UI/BH_NotificationTypes.h"
#include "BH_NotificationSubsystem.generated.h"

class APlayerController;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBH_OnNotification, const FBH_Notification&, Notification);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FBH_OnNotificationEvent);

UCLASS()
class BLACKWOODHOLLOWBETA_API UBH_NotificationSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	/** The queue of the local player that owns PC, or null. */
	static UBH_NotificationSubsystem* ForController(const APlayerController* PC);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Notifications")
	void Enqueue(const FBH_Notification& Notification);

	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Notifications")
	void PushLoot(FText ItemName, int32 Quantity);

	/** Low-priority "+N XP" toast; gains inside the batch window and while it shows add up into one line. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Notifications")
	void PushExperience(int32 Amount);

	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Notifications")
	void PushNotice(FText Title, FText Body, bool bWarning);

	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Notifications")
	void PushLevelUp(const FBH_LevelUpInfo& Info);

	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Notifications")
	void PushQuest(FText Title, FText Body);

	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Notifications")
	void PushDiscovery(FText Title, FText Body);

	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Notifications")
	void PushDeath(FText Title, FText Body);

	/** Drops the current toast and everything pending. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Notifications")
	void ClearAll();

	/** While suppressed (the local player is down) everything except death notifications is dropped. Suppressing also clears the queue. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Notifications")
	void SetSuppressed(bool bInSuppressed);

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Notifications")
	bool IsShowing() const { return bHasCurrent; }

	/** True while the local player is down (only death notifications are accepted). */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Notifications")
	bool IsSuppressed() const { return bSuppressed; }

	/** A queue widget registers while it exists, so callers can fall back (a dev message) when none is in the HUD. */
	void RegisterPresenter() { ++PresenterCount; }
	void UnregisterPresenter() { PresenterCount = FMath::Max(0, PresenterCount - 1); }
	bool HasPresenter() const { return PresenterCount > 0; }

	/** A toast starts showing. */
	UPROPERTY(BlueprintAssignable, Category = "BlackwoodHollow|Notifications")
	FBH_OnNotification OnNotificationShown;

	/** The showing loot toast absorbed more pickups. */
	UPROPERTY(BlueprintAssignable, Category = "BlackwoodHollow|Notifications")
	FBH_OnNotification OnNotificationUpdated;

	/** The showing toast starts to fade out. */
	UPROPERTY(BlueprintAssignable, Category = "BlackwoodHollow|Notifications")
	FBH_OnNotificationEvent OnNotificationEnding;

	/** The toast is gone (finished, replaced or cleared). */
	UPROPERTY(BlueprintAssignable, Category = "BlackwoodHollow|Notifications")
	FBH_OnNotificationEvent OnNotificationFinished;

private:
	struct FPending
	{
		FBH_Notification Notification;
		double ReadyTime = 0.0;
		int32 Sequence = 0;
	};

	bool Tick(float DeltaTime);
	void StartNext();
	void Show(const FBH_Notification& Notification);
	void FinishCurrent();
	float GetHoldFor(const FBH_Notification& Notification) const;
	static bool MergeLootLines(FBH_Notification& Target, const TArray<FBH_LootLine>& Lines, int32 MaxLines);
	static void RebuildLootBody(FBH_Notification& Notification);

	FTSTicker::FDelegateHandle TickHandle;
	TArray<FPending> Pending;
	FBH_Notification Current;
	bool bHasCurrent = false;
	bool bEnding = false;
	bool bSuppressed = false;
	float CurrentElapsed = 0.f;
	float CurrentDuration = 0.f;
	float CurrentAge = 0.f;
	float GapRemaining = 0.f;
	double Clock = 0.0;
	int32 NextSequence = 0;
	int32 PresenterCount = 0;
};
