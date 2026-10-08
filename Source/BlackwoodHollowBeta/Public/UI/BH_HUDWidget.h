// Blackwood Hollow - HUD widget base (GAS-driven, event based)
// Target: Unreal Engine 5.8 (C++), GAS + UMG
//
// Base class for every production HUD widget. InitializeHUD(ASC) binds
// attribute-change delegates on UAH_AttributeSet (Health, MaxHealth, Posture,
// MaxPosture, Stamina, MaxStamina) and the State.Combat.PostureBroken tag, then pushes the current
// values once so the widget is correct immediately.
//
// Nested UBH_HUDWidgets (e.g. WBP_VitalsCluster inside WBP_HUD_Main) are
// initialised automatically by their parent, so only the root needs
// InitializeHUD. Blueprint children react through the K2_On*Updated events;
// C++ children override the Handle* virtuals.
//
// Stance: UBH_CombatFunctionLibrary::ApplyOverlayPoseByDisplayName calls
// BroadcastStanceChanged. The root widget also polls the replicated GASP
// OverlayPose a few times a second so changes that only arrive by replication
// (remote clients) still reach the HUD.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayEffectTypes.h"
#include "GameplayTagContainer.h"
#include "Progression/BH_LevelUpInfo.h"
#include "BH_HUDWidget.generated.h"

class UAbilitySystemComponent;
class APlayerController;
class UBH_BossHealthBarWidget;
class UBH_XPBarWidget;
class UBH_LevelUpBannerWidget;

UCLASS(Abstract, Blueprintable)
class BLACKWOODHOLLOWBETA_API UBH_HUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Bind this widget (and every nested UBH_HUDWidget) to an AbilitySystemComponent. Safe to call again with a new ASC (respawn). */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|HUD")
	void InitializeHUD(UAbilitySystemComponent* ASC);

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|HUD")
	UAbilitySystemComponent* GetHUDAbilitySystem() const;

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|HUD")
	float GetHealth() const;

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|HUD")
	float GetMaxHealth() const;

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|HUD")
	float GetStamina() const;

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|HUD")
	float GetMaxStamina() const;

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|HUD")
	float GetPosture() const;

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|HUD")
	float GetMaxPosture() const;

	/** True while State.Combat.PostureBroken is on the ASC (or Posture has hit 0). */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|HUD")
	bool IsPostureBroken() const { return bPostureBroken; }

	/** Enum_OverlayPose display name last pushed to this widget (e.g. "SwordAndShield"). */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|HUD")
	FString GetCurrentStanceName() const { return CurrentStanceName; }

	/** Pushes a stance change into this widget (no-op if unchanged). */
	void NotifyStanceChanged(const FString& StanceName);

	/** Pushes a stance change to every live HUD widget bound to Character's ASC. */
	static void BroadcastStanceChanged(const AActor* Character, const FString& StanceName);

	/** Tells every live HUD widget owned by PC that the lock-on target changed (Target may be null). */
	static void BroadcastLockedTargetChanged(const APlayerController* PC, AActor* Target);

	/** Pushes a lock-on target change into this widget. */
	void NotifyLockedTargetChanged(AActor* Target);

	/**
	 * Phase 9: the local player reached NewLevel. Calls HandleLevelUp / K2_OnLevelUp on this widget and every nested UBH_HUDWidget.
	 * Normally called by UBH_HUDSubsystem::ShowLevelUp (which also shows the native "Level N" text when bShowNativeLevelUpText).
	 */
	void NotifyLevelUp(int32 NewLevel);

	/**
	 * Same, with the previous level and the max-stat gains. Calls HandleLevelUpDetailed (which shows LevelUpBanner when bound and then
	 * HandleLevelUp), K2_OnLevelUp, and the same on every nested UBH_HUDWidget. NotifyLevelUp(int32) forwards here with no gains.
	 */
	void NotifyLevelUp(const FBH_LevelUpInfo& Info);

	/** True when a LevelUpBanner widget is bound on this HUD (UBH_HUDSubsystem then skips its plain "Level N" text). */
	bool HasLevelUpBanner() const;

	/**
	 * When true UBH_HUDSubsystem shows a plain "Level N" text for ~2 s on level up. It is skipped automatically when the HUD has a
	 * LevelUpBanner bound; turn it OFF on WBP_HUD_Main if the widget implements its own "On Level Up" event instead.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BlackwoodHollow|HUD|Level")
	bool bShowNativeLevelUpText = true;

	// -- Boss bar settings (read by UBH_HUDSubsystem from the MAIN HUD widget; set them on WBP_HUD_Main's class defaults) ----------

	/** Boss health bar widget (WBP_BossHealthBar). Empty = no boss bar. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BlackwoodHollow|HUD|Boss")
	TSubclassOf<UBH_BossHealthBarWidget> BossBarClass;

	/** Distance in pixels from the bottom edge of the screen to the bottom of the boss bar (clears the vitals cluster). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BlackwoodHollow|HUD|Boss", meta = (ClampMin = "0.0"))
	float BossBarBottomOffset = 150.f;

	/** Seconds the bar stays after the boss dies or drops aggro on the local pawn. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BlackwoodHollow|HUD|Boss", meta = (ClampMin = "0.0"))
	float BossBarHideDelay = 1.5f;

	/** Seconds between fallback boss scans (the primary trigger is the aggro-changed event). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BlackwoodHollow|HUD|Boss", meta = (ClampMin = "0.1"))
	float BossScanInterval = 0.5f;

protected:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void NativeDestruct() override;

	// -- C++ hooks (called before the matching K2 event) -----------------------
	virtual void HandleHealthChanged(float Health, float MaxHealth) {}
	virtual void HandleStaminaChanged(float Stamina, float MaxStamina) {}
	virtual void HandlePostureChanged(float Posture, float MaxPosture) {}
	virtual void HandlePostureBrokenChanged(bool bBroken) {}
	virtual void HandleStanceChanged(const FString& StanceName) {}
	virtual void HandleLockedTargetChanged(AActor* Target) {}
	virtual void HandleLevelUp(int32 NewLevel) {}

	/**
	 * Level up with details. The default shows LevelUpBanner when it is bound, then calls HandleLevelUp(Info.NewLevel), so existing
	 * HandleLevelUp overrides keep working. The "On Level Up" Blueprint event is fired by NotifyLevelUp regardless.
	 */
	virtual void HandleLevelUpDetailed(const FBH_LevelUpInfo& Info);

	// -- Blueprint events -------------------------------------------------------
	UFUNCTION(BlueprintImplementableEvent, Category = "BlackwoodHollow|HUD", meta = (DisplayName = "On Health Updated"))
	void K2_OnHealthUpdated(float Health, float MaxHealth, float Percent);

	UFUNCTION(BlueprintImplementableEvent, Category = "BlackwoodHollow|HUD", meta = (DisplayName = "On Stamina Updated"))
	void K2_OnStaminaUpdated(float Stamina, float MaxStamina, float Percent);

	UFUNCTION(BlueprintImplementableEvent, Category = "BlackwoodHollow|HUD", meta = (DisplayName = "On Posture Updated"))
	void K2_OnPostureUpdated(float Posture, float MaxPosture, float Percent);

	/** StanceName is the Enum_OverlayPose display name ("SwordAndShield", "DualSword", ...). */
	UFUNCTION(BlueprintImplementableEvent, Category = "BlackwoodHollow|HUD", meta = (DisplayName = "On Stance Updated"))
	void K2_OnStanceUpdated(const FString& StanceName);

	UFUNCTION(BlueprintImplementableEvent, Category = "BlackwoodHollow|HUD", meta = (DisplayName = "On Posture Broken Changed"))
	void K2_OnPostureBrokenChanged(bool bBroken);

	/** Lock-on target changed (null = lock released). */
	UFUNCTION(BlueprintImplementableEvent, Category = "BlackwoodHollow|HUD", meta = (DisplayName = "On Locked Target Changed"))
	void K2_OnLockedTargetChanged(AActor* Target);

	/** The local player levelled up (the new level). Play the flourish here. */
	UFUNCTION(BlueprintImplementableEvent, Category = "BlackwoodHollow|HUD", meta = (DisplayName = "On Level Up"))
	void K2_OnLevelUp(int32 NewLevel);

	// -- Phase 9 widgets (bind by widget NAME; both optional) -------------------------------------------------------------

	/** XP bar (a WBP subclass of UBH_XPBarWidget). It binds itself to the owning player's progression component. */
	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|HUD|Level", meta = (BindWidgetOptional))
	TObjectPtr<UBH_XPBarWidget> XPBar;

	/** Level-up banner (a WBP subclass of UBH_LevelUpBannerWidget). When bound it replaces the plain "Level N" text. */
	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|HUD|Level", meta = (BindWidgetOptional))
	TObjectPtr<UBH_LevelUpBannerWidget> LevelUpBanner;

	/** When true a parent HUD widget's InitializeHUD skips this widget (it binds to some other ASC, e.g. the lock-on target). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|HUD")
	bool bExcludeFromParentInit = false;

	/** Seconds between OverlayPose replication checks (root widget only). */
	UPROPERTY(EditAnywhere, Category = "BlackwoodHollow|HUD", meta = (ClampMin = "0.05"))
	float StancePollInterval = 0.25f;

private:
	void UnbindFromASC();
	void OnAttributeChanged(const FOnAttributeChangeData& ChangeData);
	void OnPostureBrokenTagChanged(const FGameplayTag Tag, int32 NewCount);
	void PushHealth();
	void PushStamina();
	void PushPosture();
	void RefreshPostureBroken();
	void PollStance();

	TWeakObjectPtr<UAbilitySystemComponent> BoundASC;
	FDelegateHandle HealthHandle;
	FDelegateHandle MaxHealthHandle;
	FDelegateHandle StaminaHandle;
	FDelegateHandle MaxStaminaHandle;
	FDelegateHandle PostureHandle;
	FDelegateHandle MaxPostureHandle;
	FDelegateHandle PostureBrokenTagHandle;

	/** Set on widgets initialised by a parent HUD widget; only the root polls stance. */
	bool bIsNestedHUD = false;
	bool bPostureBroken = false;
	FString CurrentStanceName;
	float StancePollTimer = 0.f;
};
