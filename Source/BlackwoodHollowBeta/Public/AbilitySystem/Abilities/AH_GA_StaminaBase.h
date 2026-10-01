// Blackwood Hollow - gameplay ability base that spends Stamina on commit
// Target: Unreal Engine 5.8 (C++), GAS

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "AH_GA_StaminaBase.generated.h"

/**
 * UAH_GA_StaminaBase
 *
 * Small common base for abilities that cost Stamina (melee swings, shield bash, dodge, block raise).
 * CheckCost / ApplyCost are overridden so the standard CommitAbility() flow does everything:
 *   - activation is refused when the owner can't afford StaminaCost (CanActivateAbility -> CheckCost),
 *   - CommitAbility spends it through UAH_GE_StaminaCost (predicted on the owning client),
 *   - any spend starts the stamina regen delay (UAH_AttributeSet).
 * The maths lives in UBH_CombatFunctionLibrary::CheckStaminaCost / ApplyStaminaCost.
 */
UCLASS(Abstract, Blueprintable)
class BLACKWOODHOLLOWBETA_API UAH_GA_StaminaBase : public UGameplayAbility
{
	GENERATED_BODY()

public:
	/** Stamina spent on commit. 0 = free. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stamina", meta = (ClampMin = "0.0"))
	float StaminaCost = 0.f;

	/**
	 * false = the owner needs the FULL cost (Stamina >= StaminaCost).
	 * true  = any Stamina > 0 is enough (souls feel: the swing is allowed, stamina just floors at 0).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stamina")
	bool bAllowStaminaOvercommit = false;

	/** CheckStaminaCost for this ability's settings on the owning ASC. */
	UFUNCTION(BlueprintPure, Category = "Stamina")
	bool CanAffordStamina() const;

	virtual bool CheckCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

	virtual void ApplyCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) const override;
};
