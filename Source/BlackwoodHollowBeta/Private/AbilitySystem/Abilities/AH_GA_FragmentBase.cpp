// Blackwood Hollow - Heart-Fragment ability base (implementation)

#include "AbilitySystem/Abilities/AH_GA_FragmentBase.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystem/Effects/AH_GE_CombatEffects.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"

UAH_GA_FragmentBase::UAH_GA_FragmentBase()
{
	CooldownGameplayEffectClass = UAH_GE_Cooldown_Base::StaticClass();
	// Fragments are server-initiated: the owning client's press is forwarded to the server, which runs the ability
	// and replicates the cooldown effect back (so the cooldown tag is also visible on the client).
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	// A downed player cannot fire fragments (the melee / dodge / block / parry abilities already refuse while Dead).
	ActivationBlockedTags.AddTag(TAG_State_Combat_Dead);
}

const FGameplayTagContainer* UAH_GA_FragmentBase::GetCooldownTags() const
{
	MergedCooldownTags.Reset();
	if (const FGameplayTagContainer* ParentTags = Super::GetCooldownTags())
	{
		MergedCooldownTags.AppendTags(*ParentTags);
	}
	MergedCooldownTags.AppendTags(CooldownTags);
	return &MergedCooldownTags;
}

void UAH_GA_FragmentBase::ApplyCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo) const
{
	const UGameplayEffect* CooldownGE = GetCooldownGameplayEffect();
	if (!CooldownGE || CooldownDuration <= 0.f)
	{
		return;
	}

	FGameplayEffectSpecHandle SpecHandle = MakeOutgoingGameplayEffectSpec(Handle, ActorInfo, ActivationInfo, CooldownGE->GetClass(), GetAbilityLevel(Handle, ActorInfo));
	if (!SpecHandle.IsValid() || !SpecHandle.Data.IsValid())
	{
		return;
	}

	SpecHandle.Data->SetSetByCallerMagnitude(TAG_Data_Cooldown, CooldownDuration);
	SpecHandle.Data->DynamicGrantedTags.AppendTags(CooldownTags);
	ApplyGameplayEffectSpecToOwner(Handle, ActorInfo, ActivationInfo, SpecHandle);
}

void UAH_GA_FragmentBase::GetCooldownRemaining(const UAbilitySystemComponent* ASC, TSubclassOf<UAH_GA_FragmentBase> FragmentClass, float& Remaining, float& Duration)
{
	Remaining = 0.f;
	Duration = 0.f;

	const UAH_GA_FragmentBase* CDO = FragmentClass ? FragmentClass->GetDefaultObject<UAH_GA_FragmentBase>() : nullptr;
	if (!CDO)
	{
		return;
	}

	Duration = CDO->CooldownDuration;
	if (!ASC || CDO->CooldownTags.IsEmpty())
	{
		return;
	}

	const FGameplayEffectQuery Query = FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(CDO->CooldownTags);
	for (const TPair<float, float>& TimeAndDuration : ASC->GetActiveEffectsTimeRemainingAndDuration(Query))
	{
		if (TimeAndDuration.Key > Remaining)
		{
			Remaining = TimeAndDuration.Key;
			Duration = TimeAndDuration.Value;
		}
	}
}
