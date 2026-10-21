// Blackwood Hollow - HUD tuning (Phase 13C)
// Target: Unreal Engine 5.8 (C++)
//
// Project Settings > Game > "Blackwood Hollow HUD" (saved to DefaultGame.ini). Values are recorded in claude/balance.md.
//
// Quiet HUD: out of combat the posture bar, the target panel, a full stamina bar and an idle fragment bar fade out; health only shows
// when it is not full. Everything snaps back the instant combat starts.
// Notification queue: one toast at a time, priority death > level-up > quest > discovery > loot. Durations are HOLD times; the
// fade in / fade out are added on top.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "BH_HUDSettings.generated.h"

UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Blackwood Hollow HUD"))
class BLACKWOODHOLLOWBETA_API UBH_HUDSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UBH_HUDSettings()
	{
		CategoryName = TEXT("Game");
	}

	static const UBH_HUDSettings* Get() { return GetDefault<UBH_HUDSettings>(); }

	/** Put WBP_HUD_Main into the primary layout's Game layer. Off = the old AddToViewport(0). */
	UPROPERTY(Config, EditAnywhere, Category = "Layout")
	bool bHUDInGameLayer = true;

	UPROPERTY(Config, EditAnywhere, Category = "Quiet HUD")
	bool bQuietHUD = true;

	/** Seconds the HUD keeps its combat look after the last combat signal. */
	UPROPERTY(Config, EditAnywhere, Category = "Quiet HUD", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float OutOfCombatDelay = 4.f;

	UPROPERTY(Config, EditAnywhere, Category = "Quiet HUD", meta = (ClampMin = "0.01", ForceUnits = "s"))
	float QuietFadeOut = 0.6f;

	/** Fade in for an element's own condition (health below full, stamina spent, fragment cooling down). Combat start is instant. */
	UPROPERTY(Config, EditAnywhere, Category = "Quiet HUD", meta = (ClampMin = "0.01", ForceUnits = "s"))
	float QuietFadeIn = 0.15f;

	UPROPERTY(Config, EditAnywhere, Category = "Notifications", meta = (ClampMin = "0.01", ForceUnits = "s"))
	float NotificationFadeIn = 0.2f;

	/** How far off to the right (px) the toast starts its slide-in (ease-out). */
	UPROPERTY(Config, EditAnywhere, Category = "Notifications", meta = (ClampMin = "0.0", ForceUnits = "px"))
	float NotificationSlideDistance = 480.f;

	/** How far to the right (px) the toast drifts while it fades out. */
	UPROPERTY(Config, EditAnywhere, Category = "Notifications", meta = (ClampMin = "0.0", ForceUnits = "px"))
	float NotificationDriftDistance = 40.f;

	UPROPERTY(Config, EditAnywhere, Category = "Notifications", meta = (ClampMin = "0.01", ForceUnits = "s"))
	float NotificationFadeOut = 0.3f;

	/** Pause between two toasts so they never touch. */
	UPROPERTY(Config, EditAnywhere, Category = "Notifications", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float NotificationGap = 0.15f;

	UPROPERTY(Config, EditAnywhere, Category = "Notifications", meta = (ClampMin = "0.1", ForceUnits = "s"))
	float DeathHoldSeconds = 4.f;

	UPROPERTY(Config, EditAnywhere, Category = "Notifications", meta = (ClampMin = "0.1", ForceUnits = "s"))
	float LevelUpHoldSeconds = 3.f;

	UPROPERTY(Config, EditAnywhere, Category = "Notifications", meta = (ClampMin = "0.1", ForceUnits = "s"))
	float QuestHoldSeconds = 3.f;

	UPROPERTY(Config, EditAnywhere, Category = "Notifications", meta = (ClampMin = "0.1", ForceUnits = "s"))
	float DiscoveryHoldSeconds = 3.f;

	UPROPERTY(Config, EditAnywhere, Category = "Notifications", meta = (ClampMin = "0.1", ForceUnits = "s"))
	float LootHoldSeconds = 2.f;

	UPROPERTY(Config, EditAnywhere, Category = "Notifications", meta = (ClampMin = "0.1", ForceUnits = "s"))
	float NoticeHoldSeconds = 2.2f;

	/** A loot toast waits this long before it shows, so a chest with three items reads as one toast. */
	UPROPERTY(Config, EditAnywhere, Category = "Notifications", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float LootBatchWindow = 0.35f;

	/** A visible loot toast keeps absorbing new pickups for this long in total. */
	UPROPERTY(Config, EditAnywhere, Category = "Notifications", meta = (ClampMin = "0.5", ForceUnits = "s"))
	float LootMaxMergeAge = 4.f;

	UPROPERTY(Config, EditAnywhere, Category = "Notifications", meta = (ClampMin = "1", ClampMax = "8"))
	int32 LootMaxLines = 4;
};
