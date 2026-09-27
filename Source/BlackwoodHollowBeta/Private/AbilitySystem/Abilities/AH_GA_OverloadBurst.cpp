// Blackwood Hollow - Overload Burst gameplay ability base (implementation)

#include "AbilitySystem/Abilities/AH_GA_OverloadBurst.h"
#include "Components/BPC_HeartFragment.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystemComponent.h"

UAH_GA_OverloadBurst::UAH_GA_OverloadBurst()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;

	// Triggered directly by UBPC_HeartFragment::TryActivateOverloadBurst() via
	// Event.Combat.OverloadBurst; set on the CDO here so BP children inherit
	// the wiring, but this can also be set per-ability-spec at grant time.
	FAbilityTriggerData TriggerData;
	TriggerData.TriggerTag = FBH_GameplayTags::Get().Event_Combat_OverloadBurst;
	TriggerData.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	AbilityTriggers.Add(TriggerData);
}

bool UAH_GA_OverloadBurst::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	// Mana/cooldown gating already happened in UBPC_HeartFragment::TryActivateOverloadBurst()
	// before the triggering event was sent; this is a defensive re-check in
	// case something else ever activates this ability directly.
	if (ActorInfo && ActorInfo->AvatarActor.IsValid())
	{
		if (const UBPC_HeartFragment* HeartFragment = ActorInfo->AvatarActor->FindComponentByClass<UBPC_HeartFragment>())
		{
			return true;
		}
	}
	return false;
}

void UAH_GA_OverloadBurst::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;

	// Recharging the shield here (rather than relying solely on
	// UBPC_HeartFragment::TryActivateOverloadBurst) keeps the "burst refills
	// the shield" behavior tied to the ability actually resolving, so a
	// cancelled/interrupted activation doesn't grant a free recharge.
	if (Avatar)
	{
		if (UBPC_HeartFragment* HeartFragment = Avatar->FindComponentByClass<UBPC_HeartFragment>())
		{
			HeartFragment->RechargeBlightShield();
		}
	}

	K2_OnOverloadBurstActivated();

	if (Avatar)
	{
		Avatar->GetWorldTimerManager().SetTimer(BurstDurationTimerHandle, FTimerDelegate::CreateWeakLambda(this,
			[this, Handle, ActorInfo, ActivationInfo]()
			{
				EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
			}), BurstDuration, false);
	}
	else
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
	}
}

void UAH_GA_OverloadBurst::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (ActorInfo && ActorInfo->AvatarActor.IsValid())
	{
		ActorInfo->AvatarActor->GetWorldTimerManager().ClearTimer(BurstDurationTimerHandle);

		if (UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get())
		{
			ASC->RemoveLooseGameplayTag(FBH_GameplayTags::Get().State_Combat_Overloading);
		}
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UAH_GA_OverloadBurst::FinishBurst()
{
	// Reserved for native cleanup beyond EndAbility, if this class grows.
}
