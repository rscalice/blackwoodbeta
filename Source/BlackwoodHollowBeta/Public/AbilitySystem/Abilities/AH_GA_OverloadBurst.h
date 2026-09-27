// Blackwood Hollow - Overload Burst gameplay ability base
// Target: Unreal Engine 5.8 (C++), GAS (GameplayAbilities plugin required)
//
// Native base class for the "GA_HeartFragment_OverloadBurst" ability. Create
// GA_HeartFragment_OverloadBurst as a Blueprint child of this class (or
// rename this class and use it directly) so designers can iterate on VFX,
// montage, and Niagara hookups without touching C++, while the activation
// gate, cost/cooldown bookkeeping, and BP_BlightVolume-facing event stay
// native and consistent.
//
// Activation: UBPC_HeartFragment::TryActivateOverloadBurst() sends
// Event.Combat.OverloadBurst to the owner's AbilitySystemComponent. Grant
// this ability to the ASC with AbilityTriggers containing
// { Event.Combat.OverloadBurst, EGameplayAbilityTriggerSource::GameplayEvent }
// so that event activates it directly.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "AH_GA_OverloadBurst.generated.h"

class UBPC_HeartFragment;

UCLASS()
class BLACKWOODHOLLOWBETA_API UAH_GA_OverloadBurst : public UGameplayAbility
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

	void FinishBurst();
};
