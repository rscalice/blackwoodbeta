// Blackwood Hollow - Per-local-player HUD owner
// Target: Unreal Engine 5.8 (C++), UMG
//
// Owns the production HUD (a UBH_HUDWidget, e.g. WBP_HUD_Main) and the debug
// HUD (any UUserWidget, e.g. W_BH_DebugHUD) for one local player, and switches
// between them. Only one is visible at a time.
//
// Toggle from the console:  BH.HUD.ToggleDebug   or   BH.HUD.Debug 0|1
// or from Blueprint: UBH_CombatFunctionLibrary::ToggleDebugHUD.
//
// Boss bar: once SetupHUD has run, this subsystem also decides when the boss health bar (MainHUD->BossBarClass, one instance
// per local player) is shown. A boss is a pawn whose UBH_CombatIdentityComponent has bIsBoss; the bar shows for the NEAREST living
// boss whose replicated AggroTarget is the local pawn, fades in, and goes 1 s (BossBarHideDelay) after that boss dies or drops aggro.
// The bar is added to the main HUD's BossBarSlot (top of its TopCentreStack); the viewport is only a fallback when that slot is not
// bound. The main HUD is told which boss is presented (UBH_HUDWidget::SetPresentedBoss) and lays out the lock-on target panel itself.
// Event driven (UBH_CombatIdentityComponent::OnAnyAggroTargetChanged) with a cheap 0.5 s fallback scan.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Engine/TimerHandle.h"
#include "GameplayEffectTypes.h"
#include "Progression/BH_LevelUpInfo.h"
#include "BH_HUDSubsystem.generated.h"

class APawn;
class UUserWidget;
class UBH_HUDWidget;
class UBH_BossHealthBarWidget;
class UBH_CombatIdentityComponent;
class ABH_EnemyBase;
class UAbilitySystemComponent;
class UWorld;
class SWidget;

UCLASS()
class BLACKWOODHOLLOWBETA_API UBH_HUDSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * Creates the HUDs on first use (reused afterwards) and binds the production
	 * HUD to Pawn's AbilitySystemComponent. Pawn must be controlled by this local player.
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|HUD")
	bool SetupHUD(APawn* Pawn, TSubclassOf<UBH_HUDWidget> MainHUDClass, TSubclassOf<UUserWidget> DebugHUDClass);

	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|HUD")
	void SetDebugHUDVisible(bool bVisible);

	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|HUD")
	void ToggleDebugHUD() { SetDebugHUDVisible(!bDebugVisible); }

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|HUD")
	bool IsDebugHUDVisible() const { return bDebugVisible; }

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|HUD")
	UBH_HUDWidget* GetMainHUD() const { return MainHUD; }

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|HUD")
	UUserWidget* GetDebugHUD() const { return DebugHUD; }

	/** The boss health bar widget (created on first need; null before any boss has been shown). */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|HUD")
	UBH_BossHealthBarWidget* GetBossBar() const { return BossBar; }

	/** The boss the bar is currently presenting (may be dying / fading out), or null. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|HUD")
	AActor* GetPresentedBoss() const { return PresentedBoss.Get(); }

	/** 13E. True for WindowSeconds after the boss bar began presenting an enemy (the title card). First-time hints hold back during it. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|HUD")
	bool IsBossIntroActive(float WindowSeconds) const;

	/**
	 * Phase 9: level-up flourish. Tells the main HUD (On Level Up event) and, when UBH_HUDWidget::bShowNativeLevelUpText is set,
	 * shows a plain "Level N" text for LevelUpTextSeconds (~2 s). Safe to call before the HUD exists (the native text still shows).
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|HUD")
	void ShowLevelUp(int32 NewLevel);

	/**
	 * Phase 9: level-up flourish with the stat gains. Tells the main HUD (UBH_HUDWidget::NotifyLevelUp -> LevelUpBanner, "On Level Up" event).
	 * The plain native "Level N" text is shown only when bShowNativeLevelUpText is set AND the main HUD has no LevelUpBanner bound.
	 * ShowLevelUp(int32) forwards here with an info that has no stat gains.
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|HUD")
	void ShowLevelUpDetailed(const FBH_LevelUpInfo& Info);

	/** Seconds the native "Level N" text stays up. */
	static constexpr float LevelUpTextSeconds = 2.f;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	void ApplyVisibility();

	/** Phase 13C: puts the main HUD into the primary layout's Game layer (when the layout exists and the setting is on). */
	bool AttachMainHUDToLayout();
	void HandleLayoutReady();
	FDelegateHandle LayoutReadyHandle;

	// -- Boss bar ---------------------------------------------------------------
	void StartBossWatch();
	void StopBossWatch();
	void OnAnyAggroChanged(UBH_CombatIdentityComponent* Source, AActor* NewTarget);
	/** Phase 12G: the same event for ABH_EnemyBase enemies (the Elite crab). */
	void OnAnyEnemyAggroSeen(ABH_EnemyBase* Enemy, AActor* NewTarget);

	/** Phase 13D: Veteran growl / Elite stinger / Boss horn when a ranked enemy starts fighting the LOCAL pawn (once per enemy per cooldown). */
	void TryPlayAggroCue(AActor* Enemy, AActor* NewTarget);
	TMap<TWeakObjectPtr<AActor>, double> AggroCueTimes;
	/** Picks the nearest living boss aggroed on the local pawn and shows / schedules the hide of the bar accordingly. */
	void EvaluateBoss();
	void ShowBossBar(AActor* Boss);
	void HideBossBar();
	APawn* GetLocalPawn() const;

	UPROPERTY(Transient)
	TObjectPtr<UBH_BossHealthBarWidget> BossBar;

	TWeakObjectPtr<AActor> PresentedBoss;

	/** 13E. FPlatformTime::Seconds() when the bar last started presenting a new enemy. */
	double BossPresentedAt = -1.0e9;
	FTimerHandle BossScanTimer;
	FTimerHandle BossHideTimer;
	FDelegateHandle AggroHandle;
	FDelegateHandle EnemyAggroHandle;
	TWeakObjectPtr<UWorld> WatchWorld;

	/** The "no BossBarSlot" warning is logged once. */
	bool bWarnedNoBossSlot = false;

	/** Health watch on the presented boss so its death hides the bar on time without waiting for the next scan. */
	void BindBossHealth(AActor* Boss);
	void UnbindBossHealth();
	void OnBossHealthChanged(const FOnAttributeChangeData& ChangeData);
	TWeakObjectPtr<UAbilitySystemComponent> BossHealthASC;
	FDelegateHandle BossHealthHandle;

	void HideLevelUpText();
	TSharedPtr<SWidget> LevelUpTextWidget;
	FTimerHandle LevelUpTextTimer;
	TWeakObjectPtr<UWorld> LevelUpWorld;

	UPROPERTY(Transient)
	TObjectPtr<UBH_HUDWidget> MainHUD;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> DebugHUD;

	bool bDebugVisible = false;
};
