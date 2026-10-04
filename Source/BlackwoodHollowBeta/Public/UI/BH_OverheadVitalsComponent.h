// Blackwood Hollow - overhead enemy vitals (screen-space widget component)
// Target: Unreal Engine 5.8 (C++), UMG, GAS
//
// Lives on ENEMY pawns (BP_BH_EnemyBase_MM; never on the player). Shows a small health + posture bar over the head
// (UBH_OverheadVitalsWidget, WBP_OverheadVitals) for the LOCAL player only. Purely cosmetic, nothing is replicated: every
// machine (host and clients) evaluates the rules for its own local player controller; dedicated servers create no widget.
//
// Visibility rules (polled at UpdateInterval, ~10 Hz; the widget owns the fade):
//   hide  - owner dead, or HARD-LOCKED by the local player (the top-centre WBP_TargetVitals already shows it), or beyond MaxRange.
//   show  - owner is the local player's SOFT target (UBH_LockOnComponent::GetSoftTarget: the melee-magnetism target), or
//           the local player's pawn hit it within the last HitLingerTime seconds (signal: UBH_GCN_CombatHit runs on every
//           machine with attacker + victim -> NotifyHitByLocalPlayer).
//   fade  - FadeInTime in, FadeOutTime out after the conditions end (hit-based visibility therefore lingers HitLingerTime). The fade is
//           advanced in this component's tick (a screen-space widget is collapsed, so never ticks, while its owner is off screen).
// Console: bh.OverheadVitals.Debug 1 forces the bar on every enemy (still hidden when dead / hard-locked).

#pragma once

#include "CoreMinimal.h"
#include "Components/WidgetComponent.h"
#include "TimerManager.h"
#include "BH_OverheadVitalsComponent.generated.h"

class UBH_OverheadVitalsWidget;
class UAbilitySystemComponent;
class APlayerController;

UCLASS(ClassGroup = (BlackwoodHollow), meta = (BlueprintSpawnableComponent))
class BLACKWOODHOLLOWBETA_API UBH_OverheadVitalsComponent : public UWidgetComponent
{
	GENERATED_BODY()

public:
	UBH_OverheadVitalsComponent();

	virtual void OnRegister() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	 * Called by the hit cue on every machine. If Attacker is a locally controlled pawn and Victim carries an overhead
	 * component, that bar is shown for HitLingerTime seconds.
	 */
	static void NotifyHitByLocalPlayer(const AActor* Attacker, const AActor* Victim);

	/** Registers a hit by the local player now. */
	void MarkHitByLocalPlayer();

	/** Centre of the bar above the TOP of the owner's capsule (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OverheadVitals")
	float HeightAboveCapsuleTop = 115.f;

	/** Beyond this distance (cm) from the local player the bar is hidden. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OverheadVitals", meta = (ClampMin = "0.0"))
	float MaxRange = 2500.f;

	/** Seconds the bar stays after the local player last hit this enemy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OverheadVitals", meta = (ClampMin = "0.0"))
	float HitLingerTime = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OverheadVitals", meta = (ClampMin = "0.01"))
	float FadeInTime = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OverheadVitals", meta = (ClampMin = "0.01"))
	float FadeOutTime = 0.4f;

	/** Seconds between visibility evaluations. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OverheadVitals", meta = (ClampMin = "0.02"))
	float UpdateInterval = 0.1f;

	/** True once the widget has been bound to the owner's ability system (diagnostics). */
	UFUNCTION(BlueprintPure, Category = "OverheadVitals")
	bool IsBoundToOwner() const { return bBound; }

	/** True while the last evaluation wanted the bar shown (diagnostics). */
	UFUNCTION(BlueprintPure, Category = "OverheadVitals")
	bool IsBarWanted() const { return bWanted; }

	/** Current widget opacity, 0 when there is no widget (diagnostics). */
	UFUNCTION(BlueprintPure, Category = "OverheadVitals")
	float GetBarOpacity() const { return CurrentAlpha; }

private:
	void UpdateVisibility();
	bool EvaluateWanted() const;
	bool IsOwnerDead(const UAbilitySystemComponent* ASC) const;
	void TryBind();
	APlayerController* FindLocalController() const;

	FTimerHandle UpdateTimer;
	bool bBound = false;
	bool bWanted = false;
	/** Fade state, advanced every frame in TickComponent (linear: FadeInTime / FadeOutTime seconds for a full sweep). */
	float CurrentAlpha = 0.f;
	bool bCosmeticMachine = false;
	double LastLocalHitTime = -1.0e9;
};
