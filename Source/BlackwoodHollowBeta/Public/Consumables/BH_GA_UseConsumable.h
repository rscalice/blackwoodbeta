// Blackwood Hollow - generic "use a consumable" ability (Phase 11D)
// Target: Unreal Engine 5.8 (C++), GAS
//
// One ability for every consumable. Activated by the gameplay event Event.Consumable.Use whose payload OptionalObject is the Narrative item
// class to use (UBH_ConsumableLibrary::ActivateOnServer sends it; the radial wheel / debug command reach it through
// UBH_ConsumableLibrary::RequestUseConsumable -> UBH_InteractorComponent::ServerUseConsumable). What the item does comes from its
// FBH_ConsumableDefinition (UBH_ConsumableLibrary::FindDefinition): montage, UseDuration, and the effect that lands at the end.
//
// SERVER (authority) flow:
//   1. resolve item class + definition; refuse unless the player's inventory holds >= 1 (server-side check, the client is never trusted),
//   2. CommitAbility, then REMOVE EXACTLY ONE item,
//   3. play the montage (play rate scaled so it lasts exactly UseDuration) and wait UseDuration,
//   4. apply the effect (heal over time / sanctuary) and end.
// An interruption BEFORE step 4 (posture break, death-adjacent cancels) refunds the item. Dying while drinking does not refund.
// The owning client runs the same ability (NetExecutionPolicy ServerInitiated, like the hit reaction) only to play the montage locally.
//
// LOCKS: grants State.Action.Consuming for the whole use. Attack, dodge, block, parry, shield bash and hit reaction list that tag in their
// ActivationBlockedTags (so none can START and, because nothing is allowed to cancel by that tag, none can interrupt either), and this
// ability's BlockAbilitiesWithTag does the same by asset tag. Dodge's CancelAbilitiesWithTag only names melee / block / parry, and the
// hit reaction no longer activates while Consuming (damage still lands, there is simply no flinch), so the use cannot be cancelled by
// them. The one deliberate exception is the posture break, which cancels Ability.Consumable.Use (a guard break is meant to cost you).

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Consumables/BH_ConsumableTypes.h"
#include "BH_GA_UseConsumable.generated.h"

class UAbilityTask_PlayMontageAndWait;
class UAbilityTask_WaitDelay;
class UNarrativeItem;

UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API UBH_GA_UseConsumable : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UBH_GA_UseConsumable();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	/** The use time is over: server applies the effect, everybody ends. */
	UFUNCTION()
	void OnUseTimeElapsed();

	/** The montage was interrupted / cancelled before the use finished. */
	UFUNCTION()
	void OnMontageInterrupted();

	/** Ends this ability with the stored activation data. */
	void FinishUse(bool bWasCancelled);

	UPROPERTY(Transient)
	TSubclassOf<UNarrativeItem> ActiveItemClass;

	UPROPERTY(Transient)
	FBH_ConsumableDefinition ActiveDefinition;

	/** Server: one item was removed at the start (refunded if the use is cancelled before the effect lands). */
	bool bItemConsumed = false;

	/** Server: the effect already landed (nothing to refund any more). */
	bool bEffectApplied = false;

	UPROPERTY(Transient)
	TObjectPtr<UAbilityTask_PlayMontageAndWait> MontageTask;

	UPROPERTY(Transient)
	TObjectPtr<UAbilityTask_WaitDelay> DelayTask;
};
