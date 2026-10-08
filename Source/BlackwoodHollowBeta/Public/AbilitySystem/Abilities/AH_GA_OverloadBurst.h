// Blackwood Hollow - Overload Burst gameplay ability base
// Target: Unreal Engine 5.8 (C++), GAS (GameplayAbilities plugin required)
//
// Heart-Fragment ability (Phase 10B rework): a pulse of Heart-Fragment energy that
//   * clears every BP_BlightVolume within FogClearRadius (ABP_BlightVolume::ClearFog: the fog regrows after the volume's
//     RegrowSeconds, or stays clear when the volume is bClearPermanently), and
//   * staggers nearby corrupted enemies (enemy team) within BurstRadius through the existing hit-reaction path.
// It grants no protective buff and does not touch the Blight meter. All gameplay runs on the server. Usable directly or via a
// Blueprint child (VFX / montage hooks live in K2_OnOverloadBurstActivated).
//
// Activation: it is a UAH_GA_FragmentBase, so it is granted and fired by the
// Heart-Fragment loadout (UBPC_HeartFragment::TryActivateFragment, keys 1-5).
// There is no event trigger and no resource cost; the 45 s cooldown is CooldownDuration (set in the constructor, applied by
// UAH_GA_FragmentBase::ApplyCooldown through UAH_GE_Cooldown_Base with tag Cooldown.Fragment.OverloadBurst). On activation
// (authority) it still broadcasts Event.Combat.OverloadBurst on its ASC for anything that wants to listen (informational).

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/AH_GA_FragmentBase.h"
#include "AH_GA_OverloadBurst.generated.h"

UCLASS()
class BLACKWOODHOLLOWBETA_API UAH_GA_OverloadBurst : public UAH_GA_FragmentBase
{
	GENERATED_BODY()

public:
	UAH_GA_OverloadBurst();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	/** Radius (cm) around the caster within which BP_BlightVolume actors (any part of their box) are cleared. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "OverloadBurst", meta = (ClampMin = "0.0"))
	float FogClearRadius = 1500.f;

	/** Radius (cm) around the caster within which corrupted (enemy-team) pawns are staggered. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "OverloadBurst", meta = (ClampMin = "0.0"))
	float BurstRadius = 800.f;

	/** EventMagnitude of the Event.Combat.DamageReceived stagger event sent to each enemy (UAH_GA_HitReaction::MinimumDamageToReact filters on it). No damage is dealt. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "OverloadBurst", meta = (ClampMin = "0.0"))
	float EnemyStaggerMagnitude = 1.f;

	/** How long the ability holds State.Combat.Overloading before ending itself (montage/VFX window). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "OverloadBurst")
	float BurstDuration = 1.2f;

protected:
	/** Blueprint hook fired right after the native activation logic (fog cleared, enemies staggered) runs. Wire VFX/Niagara/montage here. */
	UFUNCTION(BlueprintImplementableEvent, Category = "OverloadBurst", meta = (DisplayName = "On Overload Burst Activated"))
	void K2_OnOverloadBurstActivated();

private:
	FTimerHandle BurstDurationTimerHandle;

	/** True while this activation holds a State.Combat.Overloading loose tag (removed again in EndAbility). */
	bool bHoldingOverloadingTag = false;

	/** Server: ClearFog() on every BP_BlightVolume within FogClearRadius of Center. */
	void ClearNearbyFog(const AActor* Avatar, const FVector& Center) const;

	/** Server: Event.Combat.DamageReceived (stagger) to every living enemy-team pawn with an ASC within BurstRadius of Center. */
	void StaggerNearbyEnemies(AActor* Avatar, const FVector& Center) const;

	void FinishBurst();
};
