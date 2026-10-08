// Blackwood Hollow - player progression (XP / level) component
// Target: Unreal Engine 5.8 (C++), GAS
//
// Phase 9. A default subobject of ABH_PlayerState (so it survives pawn respawns and replicates to the owning client). It owns the
// replicated CurrentXP and Level and, SERVER side, turns them into pawn stats:
//   * pushes Level into the pawn ASC's UAH_AttributeSet::Level attribute,
//   * sets the BASE MaxHealth / MaxPosture / MaxStamina from the player curve table (UBH_RPGSettings::PlayerScalingTable) at that
//     level (a missing table or row leaves the current value alone),
//   * refills Health / Posture / Stamina (not while the pawn is dead).
// This is re-applied whenever the PlayerState gets a new pawn (possess / respawn), and from SetupCombatCharacter.
//
// Kill XP: GrantKillXP gives an amount to every living player pawn within UBH_RPGSettings::XPShareRadius of a position.
//
// Events (BlueprintAssignable): OnXPChanged, OnLevelUp and OnLevelUpDetailed fire on the server and on the owning client.
//   * OnLevelUp(NewLevel) is the original event and keeps firing.
//   * OnLevelUpDetailed(FBH_LevelUpInfo) adds the previous level and the max-stat gains (computed on the server from the curves).
// The owning client also shows the flourish through UBH_HUDSubsystem::ShowLevelUpDetailed (UBH_LevelUpBannerWidget on WBP_HUD_Main,
// or the native "Level N" fallback; WBP_HUD_Main also gets the "On Level Up" event).
//
// A real level up (not the initial replication, not the respawn re-apply in ApplyToPawn) also executes the GameplayCue
// GameplayCue.Player.LevelUp on the pawn's ASC from the server, so every client sees it. The cue asset is a Blueprint notify.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/TimerHandle.h"
#include "Progression/BH_LevelUpInfo.h"
#include "BH_ProgressionComponent.generated.h"

class APawn;
class APlayerState;
class UAbilitySystemComponent;

/** XP changed (level up, XP gained, or level set). XPToNextLevel is 0 at the level cap. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FBH_OnXPChanged, int32, CurrentXP, int32, XPToNextLevel);

/** The player reached NewLevel (one broadcast per level gained at once: the final level only). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBH_OnLevelUp, int32, NewLevel);

/** Same moment as FBH_OnLevelUp, with the previous level and the max-stat gains. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBH_OnLevelUpDetailed, const FBH_LevelUpInfo&, Info);

UCLASS(ClassGroup = (BlackwoodHollow), meta = (BlueprintSpawnableComponent))
class BLACKWOODHOLLOWBETA_API UBH_ProgressionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBH_ProgressionComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** The progression component for a PlayerState, a Pawn (via its PlayerState) or a Controller. Null when there is none. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Progression")
	static UBH_ProgressionComponent* FindProgression(const AActor* Actor);

	// -- State ------------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Progression")
	int32 GetLevel() const { return Level; }

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Progression")
	int32 GetCurrentXP() const { return CurrentXP; }

	/** XP needed to go from the current level to the next (0 at the level cap). */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Progression")
	int32 GetXPToNextLevel() const;

	/** CurrentXP / XPToNextLevel in 0..1 (1 at the level cap). */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Progression")
	float GetXPProgress01() const;

	// -- Server API -------------------------------------------------------------------------

	/** SERVER only. Adds XP; levels up as many times as the XP allows and stops at the cap. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Progression")
	void AddXP(int32 Amount);

	/** SERVER only. Sets the level (clamped to 1..MaxLevel), resets CurrentXP to 0 and re-applies the stats. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Progression")
	void SetLevel(int32 NewLevel);

	/**
	 * SERVER only. Writes Level and the curve stats into Pawn's ability system and refills Health / Posture / Stamina.
	 * If the pawn's attribute set does not exist yet it retries for a few seconds. Called automatically on possess / respawn.
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Progression")
	void ApplyToPawn(APawn* Pawn);

	/**
	 * SERVER only. Gives Amount XP to every living player pawn within UBH_RPGSettings::XPShareRadius of Source
	 * (a radius of 0 means no distance limit).
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Progression")
	static void GrantKillXP(const AActor* Source, int32 Amount);

	/**
	 * What a level change from FromLevel to ToLevel is worth in base max stats (player curve rows evaluated at both levels).
	 * A missing table or row gives a gain of 0 for that stat.
	 */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Progression")
	static FBH_LevelUpInfo BuildLevelUpInfo(int32 FromLevel, int32 ToLevel);

	// -- Events -----------------------------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "BlackwoodHollow|Progression")
	FBH_OnXPChanged OnXPChanged;

	UPROPERTY(BlueprintAssignable, Category = "BlackwoodHollow|Progression")
	FBH_OnLevelUp OnLevelUp;

	/** Fires together with OnLevelUp, with the previous level and the max-stat gains. */
	UPROPERTY(BlueprintAssignable, Category = "BlackwoodHollow|Progression")
	FBH_OnLevelUpDetailed OnLevelUpDetailed;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(ReplicatedUsing = OnRep_CurrentXP, VisibleInstanceOnly, BlueprintReadOnly, Category = "BlackwoodHollow|Progression")
	int32 CurrentXP = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Level, VisibleInstanceOnly, BlueprintReadOnly, Category = "BlackwoodHollow|Progression")
	int32 Level = 1;

	UFUNCTION()
	void OnRep_CurrentXP();

	UFUNCTION()
	void OnRep_Level();

	/** Server -> owning client: a real level up happened (not sent for the initial replication). Broadcasts OnLevelUp / OnLevelUpDetailed and shows the flourish. */
	UFUNCTION(Client, Reliable)
	void ClientNotifyLevelUp(const FBH_LevelUpInfo& Info);

private:
	UFUNCTION()
	void HandlePawnSet(APlayerState* Player, APawn* NewPawn, APawn* OldPawn);
	void RetryApplyToPawn();
	void BroadcastXPChanged();
	void NotifyLevelUp(int32 PreviousLevel, int32 NewLevel);
	void ShowLevelUpFlourish(const FBH_LevelUpInfo& Info) const;
	void ExecuteLevelUpCue(int32 NewLevel) const;
	UAbilitySystemComponent* FindPawnASC(const APawn* Pawn) const;

	TWeakObjectPtr<APawn> PendingPawn;
	FTimerHandle RetryTimer;
	bool bBoundPawnSet = false;
	int32 RetryCount = 0;

	/** Seconds between attempts / maximum attempts while the pawn's attribute set is not there yet. */
	static constexpr float RetryInterval = 0.25f;
	static constexpr int32 MaxRetries = 40;
};
