// Blackwood Hollow - the crab's three abilities (Phase 8C)
// Target: Unreal Engine 5.8 (C++), GAS
//
//   UAH_GA_CrabJab       fast, low damage, high posture damage: short wind-up, lunge, timed sweep.
//   UAH_GA_CrabPinch     telegraphed at the target's position (UBH_TelegraphComponent), then lunge, then an area hit that applies the
//                        Blight DoT (UAH_GE_BlightDoT) to a victim that neither blocked nor parried it.
//   UAH_GA_CrabSidestep  lateral dash away from the target's line (random side, wall-checked); not while attacking / posture-broken.
//
// Poses are NOT played here: each step reports an EBH_CrabActionPhase (JabWindup, JabStrike, PinchWindup, PinchSnap, Sidestep)
// through ABH_EnemyCrab::SetAbilityPhase and the ABP shows it. Grant them in BP_BH_Enemy_Crab::DefaultAbilities and let the
// Behavior Tree start them (BTTask_BH_ActivateAbilityByClass). All timings are EditDefaultsOnly on the Blueprint children.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/AH_GA_CrabAttackBase.h"
#include "AH_GA_CrabAbilities.generated.h"

/** Quick stab. Asset tag Ability.Combat.MeleeAttack (hit reactions / posture break cancel it), owns State.Combat.Attacking. */
UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API UAH_GA_CrabJab : public UAH_GA_CrabAttackBase
{
	GENERATED_BODY()

public:
	UAH_GA_CrabJab();

	/** Telegraph before the strike (JabWindup phase). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Crab|Jab", meta = (ClampMin = "0.0"))
	float WindupTime = 0.30f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Crab|Jab", meta = (ClampMin = "0.0"))
	float LungeSpeed = 900.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Crab|Jab", meta = (ClampMin = "0.01"))
	float LungeDuration = 0.18f;

	/** Length of the hit window (starts with the lunge). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Crab|Jab", meta = (ClampMin = "0.01"))
	float HitWindow = 0.16f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Crab|Jab", meta = (ClampMin = "1.0"))
	float HitRadius = 60.f;

	/** The hit sphere is centred this far in front of the crab. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Crab|Jab", meta = (ClampMin = "0.0"))
	float HitReach = 120.f;

protected:
	virtual void BeginAttack() override;
	virtual void HitScanTick() override;
	virtual void OnHitScanFinished() override;

private:
	void OnWindupFinished();
};

/** Telegraphed pincer slam that leaves Blight on the victim. */
UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API UAH_GA_CrabPinch : public UAH_GA_CrabAttackBase
{
	GENERATED_BODY()

public:
	UAH_GA_CrabPinch();

	/** Seconds the ground warning fills (PinchWindup phase is held for this long). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Crab|Pinch", meta = (ClampMin = "0.1"))
	float TelegraphDuration = 1.2f;

	/** Radius (cm) of the telegraphed area and of the area hit. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Crab|Pinch", meta = (ClampMin = "10.0"))
	float TelegraphRadius = 220.f;

	/** The lunge covers at most this much of the way to the telegraphed point. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Crab|Pinch", meta = (ClampMin = "0.0"))
	float LungeMaxDistance = 300.f;

	/** The lunge stops this far short of the telegraphed point. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Crab|Pinch", meta = (ClampMin = "0.0"))
	float StopShortDistance = 60.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Crab|Pinch", meta = (ClampMin = "0.01"))
	float LungeDuration = 0.22f;

	/** Seconds after the lunge starts at which the area hit lands (PinchSnap). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Crab|Pinch", meta = (ClampMin = "0.01"))
	float SnapHitDelay = 0.2f;

	/** Blight damage per second BEFORE the victim's BlightResistance, and how long it lasts. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Crab|Pinch|Blight", meta = (ClampMin = "0.0"))
	float BlightDPS = 6.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Crab|Pinch|Blight", meta = (ClampMin = "0.1"))
	float BlightDuration = 4.f;

	/** false (default): a blocked hit leaves no Blight; a parried hit never does. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Crab|Pinch|Blight")
	bool bApplyBlightWhenBlocked = false;

protected:
	virtual void BeginAttack() override;
	virtual void OnVictimResolved(AActor* Victim, const FBH_CrabHitOutcome& Outcome) override;

private:
	void OnTelegraphFinished();
	void OnSnapHit();

	FVector TargetPoint = FVector::ZeroVector;
};

/** Short lateral dash (dodges the player's line of attack). Shares Ability.Combat.Dodge / State.Combat.Dodging with the player dodge. */
UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API UAH_GA_CrabSidestep : public UAH_GA_CrabAttackBase
{
	GENERATED_BODY()

public:
	UAH_GA_CrabSidestep();

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Crab|Sidestep", meta = (ClampMin = "10.0"))
	float Distance = 350.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Crab|Sidestep", meta = (ClampMin = "0.05"))
	float Duration = 0.28f;

	/** Smallest clear distance (cm) a side needs for the dash to happen. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Crab|Sidestep", meta = (ClampMin = "0.0"))
	float MinClearDistance = 120.f;

	/** Grant State.Combat.Invulnerable for IFrameDuration (melee passes through the crab). Off by default. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Crab|Sidestep")
	bool bGrantIFrames = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Crab|Sidestep", meta = (EditCondition = "bGrantIFrames", ClampMin = "0.0"))
	float IFrameDuration = 0.2f;

protected:
	virtual void BeginAttack() override;

private:
	float ClearDistanceAlong(const FVector& Direction) const;
	void OnDashFinished();
	void OnIFramesFinished();
	void RemoveIFrames();

	FTimerHandle IFrameTimer;
	bool bIFramesActive = false;
};
