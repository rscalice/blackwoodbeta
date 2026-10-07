// Blackwood Hollow - Overload Burst gameplay ability base
// Target: Unreal Engine 5.8 (C++), GAS (GameplayAbilities plugin required)
//
// Heart-Fragment ability: refills the Blight shield and suppresses nearby Blight
// volumes. Usable directly or via a Blueprint child (VFX / montage hooks live in
// K2_OnOverloadBurstActivated).
//
// Activation: it is a UAH_GA_FragmentBase, so it is granted and fired by the
// Heart-Fragment loadout (UBPC_HeartFragment::TryActivateFragment, keys 1-5).
// There is no event trigger and no resource cost; the 20 s cooldown comes from
// UAH_GA_FragmentBase (tag Cooldown.Fragment.OverloadBurst). On activation (authority)
// it still broadcasts Event.Combat.OverloadBurst on its ASC for BP_BlightVolume.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/AH_GA_FragmentBase.h"
#include "AH_GA_OverloadBurst.generated.h"

class UBPC_HeartFragment;

UCLASS()
class BLACKWOODHOLLOWBETA_API UAH_GA_OverloadBurst : public UAH_GA_FragmentBase
{
	GENERATED_BODY()

public:
	UAH_GA_OverloadBurst();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags,
		FGameplayTagContainer* OptionalRelevantTags) const override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	/** Radius (world units) of the burst's push/damage effect against nearby Blight sources. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "OverloadBurst")
	float BurstRadius = 800.f;

	/** How long the ability holds State.Combat.Overloading before ending itself (montage/VFX window). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "OverloadBurst")
	float BurstDuration = 1.2f;

protected:
	/** Blueprint hook fired right after native activation logic (shield recharge, tag application) runs. Wire VFX/Niagara/montage here. */
	UFUNCTION(BlueprintImplementableEvent, Category = "OverloadBurst", meta = (DisplayName = "On Overload Burst Activated"))
	void K2_OnOverloadBurstActivated();

private:
	FTimerHandle BurstDurationTimerHandle;

	/** True while this activation holds a State.Combat.Overloading loose tag (removed again in EndAbility). */
	bool bHoldingOverloadingTag = false;

	void FinishBurst();
};
