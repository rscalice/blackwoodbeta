// Blackwood Hollow - Heart-Fragment ability base
// Target: Unreal Engine 5.8 (C++), GAS (GameplayAbilities plugin required)
//
// Base class for every ability that can sit in a Heart-Fragment loadout slot
// (UBPC_HeartFragment::EquippedFragments). Standard per-ability GAS cooldown:
//   * No cost (no stamina).
//   * Cooldown = UAH_GE_Cooldown_Base applied on commit, duration = CooldownDuration
//     (SetByCaller Data.Cooldown), granting this ability's CooldownTags dynamically.
//   * Two fragments sharing a cooldown tag share the cooldown.
// FragmentName / FragmentIcon / SlotIndexHint are display data for the future radial skill bar.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayTagContainer.h"
#include "AH_GA_FragmentBase.generated.h"

class UAbilitySystemComponent;
class UTexture2D;

UCLASS(Abstract)
class BLACKWOODHOLLOWBETA_API UAH_GA_FragmentBase : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UAH_GA_FragmentBase();

	/** Cooldown length in seconds, applied when the ability commits. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fragment")
	float CooldownDuration = 10.f;

	/** Tags granted (dynamically) for the duration of the cooldown, e.g. Cooldown.Fragment.OverloadBurst. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fragment")
	FGameplayTagContainer CooldownTags;

	/** Display name for the skill bar. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fragment|UI")
	FText FragmentName;

	/** Icon for the (future) radial skill bar. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fragment|UI")
	TObjectPtr<UTexture2D> FragmentIcon;

	/**
	 * Phase 8B: category of this fragment (Fragment.Category.Vitality / Offensive / BlightResist). A Heart-Fragment slot
	 * only accepts fragments whose category matches the slot's restriction (see ABH_PlayerState::CanEquipInSlot).
	 * An invalid tag means "uncategorised": it fits no restricted slot.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fragment", meta = (Categories = "Fragment.Category"))
	FGameplayTag FragmentCategory;

	/** Preferred loadout slot (0-based) for the skill bar; informational only. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Fragment|UI")
	int32 SlotIndexHint = 0;

	virtual const FGameplayTagContainer* GetCooldownTags() const override;
	virtual void ApplyCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) const override;

	/**
	 * Cooldown state of FragmentClass on ASC: longest remaining time among active effects granting its CooldownTags.
	 * Remaining = 0 when ready. Duration = that effect's total duration (or the class's CooldownDuration when ready).
	 */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Fragments")
	static void GetCooldownRemaining(const UAbilitySystemComponent* ASC, TSubclassOf<UAH_GA_FragmentBase> FragmentClass, float& Remaining, float& Duration);

private:
	/** Scratch container returned by GetCooldownTags (parent tags + CooldownTags). */
	mutable FGameplayTagContainer MergedCooldownTags;
};
