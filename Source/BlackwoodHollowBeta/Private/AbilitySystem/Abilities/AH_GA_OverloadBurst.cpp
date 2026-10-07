// Blackwood Hollow - Overload Burst gameplay ability base (implementation)

#include "AbilitySystem/Abilities/AH_GA_OverloadBurst.h"
#include "Components/BPC_HeartFragment.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystemComponent.h"

UAH_GA_OverloadBurst::UAH_GA_OverloadBurst()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;

	// Heart-Fragment loadout data (no cost, 20 s cooldown via UAH_GA_FragmentBase).
	CooldownDuration = 20.f;
	CooldownTags.AddTag(TAG_Cooldown_Fragment_OverloadBurst);
	FragmentName = NSLOCTEXT("BlackwoodHollow", "Fragment_OverloadBurst", "Overload Burst");
	SlotIndexHint = 0;
	FragmentCategory = TAG_Fragment_Category_Offensive;
}

bool UAH_GA_OverloadBurst::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	// Cooldown is enforced by Super (UAH_GA_FragmentBase cooldown tags); there is no cost.
	// The owner must carry a Heart-Fragment component (shield recharge target).
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

	// Recharging the shield here keeps the "burst refills the shield" behavior tied to
	// the ability actually resolving, so a cancelled/interrupted activation doesn't
	// grant a free recharge.
	if (ASC)
	{
		ASC->AddLooseGameplayTag(FBH_GameplayTags::Get().State_Combat_Overloading);
		bHoldingOverloadingTag = true;
	}

	if (Avatar)
	{
		if (UBPC_HeartFragment* HeartFragment = Avatar->FindComponentByClass<UBPC_HeartFragment>())
		{
			HeartFragment->RechargeBlightShield();
		}
	}

	// BP_BlightVolume subscribes to this event on nearby ASCs to suppress its fog.
	if (ASC && Avatar && Avatar->HasAuthority())
	{
		FGameplayEventData EventData;
		EventData.EventTag = FBH_GameplayTags::Get().Event_Combat_OverloadBurst;
		EventData.Instigator = Avatar;
		EventData.Target = Avatar;
		EventData.EventMagnitude = BurstRadius;
		ASC->HandleGameplayEvent(EventData.EventTag, &EventData);
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
			if (bHoldingOverloadingTag)
			{
				ASC->RemoveLooseGameplayTag(FBH_GameplayTags::Get().State_Combat_Overloading);
			}
		}
		bHoldingOverloadingTag = false;
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UAH_GA_OverloadBurst::FinishBurst()
{
	// Reserved for native cleanup beyond EndAbility, if this class grows.
}
