// Blackwood Hollow - gameplay ability base that spends Stamina on commit (implementation)

#include "AbilitySystem/Abilities/AH_GA_StaminaBase.h"
#include "AbilitySystem/BH_CombatFunctionLibrary.h"
#include "AbilitySystemComponent.h"

bool UAH_GA_StaminaBase::CanAffordStamina() const
{
	return UBH_CombatFunctionLibrary::CheckStaminaCost(GetAbilitySystemComponentFromActorInfo(), StaminaCost, bAllowStaminaOvercommit);
}

bool UAH_GA_StaminaBase::CheckCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CheckCost(Handle, ActorInfo, OptionalRelevantTags))
	{
		return false;
	}

	UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	return UBH_CombatFunctionLibrary::CheckStaminaCost(ASC, StaminaCost, bAllowStaminaOvercommit);
}

void UAH_GA_StaminaBase::ApplyCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo) const
{
	Super::ApplyCost(Handle, ActorInfo, ActivationInfo);

	// Predicted on the owning client (the commit runs inside the activation's prediction window), authoritative on the server.
	if (StaminaCost > 0.f && ActorInfo && HasAuthorityOrPredictionKey(ActorInfo, &ActivationInfo))
	{
		UBH_CombatFunctionLibrary::ApplyStaminaCost(ActorInfo->AbilitySystemComponent.Get(), StaminaCost);
	}
}
