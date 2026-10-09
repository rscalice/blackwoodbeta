// Blackwood Hollow - generic "use a consumable" ability (implementation)

#include "Consumables/BH_GA_UseConsumable.h"
#include "Consumables/BH_ConsumableLibrary.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "Loot/BH_LootLibrary.h"
#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "Animation/AnimMontage.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "NarrativeItem.h"

DEFINE_LOG_CATEGORY_STATIC(LogBHUseConsumable, Log, All);

UBH_GA_UseConsumable::UBH_GA_UseConsumable()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	// The server decides (item check, removal, effect); the owning client is told to start and only plays the montage.
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;

	FGameplayTagContainer DefaultAssetTags;
	DefaultAssetTags.AddTag(TAG_Ability_Consumable_Use);
	SetAssetTags(DefaultAssetTags);

	ActivationOwnedTags.AddTag(TAG_State_Action_Consuming);

	// Cannot start while staggered / broken / dead / mid-roll / mid-swing / guarding, nor while already consuming (blocks a double use).
	ActivationBlockedTags.AddTag(TAG_State_Combat_Staggered);
	ActivationBlockedTags.AddTag(TAG_State_Combat_PostureBroken);
	ActivationBlockedTags.AddTag(TAG_State_Combat_Dead);
	ActivationBlockedTags.AddTag(TAG_State_Combat_Dodging);
	ActivationBlockedTags.AddTag(TAG_State_Combat_Attacking);
	ActivationBlockedTags.AddTag(TAG_State_Combat_Blocking);
	ActivationBlockedTags.AddTag(TAG_State_Combat_Parrying);
	ActivationBlockedTags.AddTag(TAG_State_Action_Consuming);

	// While running, nothing in the combat set may start either (the same by asset tag; the abilities also list State.Action.Consuming themselves).
	BlockAbilitiesWithTag.AddTag(TAG_Ability_Combat_MeleeAttack);
	BlockAbilitiesWithTag.AddTag(TAG_Ability_Combat_Block);
	BlockAbilitiesWithTag.AddTag(TAG_Ability_Combat_Parry);
	BlockAbilitiesWithTag.AddTag(TAG_Ability_Combat_Dodge);
	BlockAbilitiesWithTag.AddTag(TAG_Ability_Combat_ShieldBash);

	FAbilityTriggerData Trigger;
	Trigger.TriggerTag = TAG_Event_Consumable_Use.GetTag();
	Trigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	AbilityTriggers.Add(Trigger);
}

FGameplayTagContainer UBH_GA_UseConsumable::GetUseBlockedByTags() const
{
	return ActivationBlockedTags;
}

void UBH_GA_UseConsumable::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	bItemConsumed = false;
	bEffectApplied = false;
	ActiveItemClass = nullptr;
	ActiveDefinition = FBH_ConsumableDefinition();

	const bool bAuthority = HasAuthority(&ActivationInfo);

	// Which item? The event payload names the item class (the client copy receives the same payload from the server).
	const UClass* PayloadClass = TriggerEventData ? Cast<UClass>(TriggerEventData->OptionalObject.Get()) : nullptr;
	if (PayloadClass && PayloadClass->IsChildOf(UNarrativeItem::StaticClass()))
	{
		ActiveItemClass = const_cast<UClass*>(PayloadClass);
	}

	const bool bHaveDefinition = ActiveItemClass && UBH_ConsumableLibrary::FindDefinition(ActiveItemClass, ActiveDefinition);

	if (bAuthority)
	{
		if (!bHaveDefinition)
		{
			UE_LOG(LogBHUseConsumable, Warning, TEXT("UseConsumable: no consumable definition for '%s'."), *GetNameSafe(ActiveItemClass));
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}

		const APawn* AvatarPawn = Cast<APawn>(GetAvatarActorFromActorInfo());
		APlayerState* PlayerState = AvatarPawn ? AvatarPawn->GetPlayerState() : nullptr;
		if (!PlayerState)
		{
			UE_LOG(LogBHUseConsumable, Log, TEXT("UseConsumable: %s has no player state."), *GetNameSafe(AvatarPawn));
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}

		// Authoritative refusal check (item count, refuse-at-full-health) BEFORE anything is committed or consumed.
		EBH_ConsumableRefusal Refusal = EBH_ConsumableRefusal::None;
		if (!UBH_ConsumableLibrary::CheckUseAllowed(AvatarPawn, ActiveItemClass, ActiveDefinition, Refusal))
		{
			UE_LOG(LogBHUseConsumable, Log, TEXT("UseConsumable: %s refused to use %s (reason %d)."), *GetNameSafe(AvatarPawn), *GetNameSafe(ActiveItemClass), static_cast<int32>(Refusal));
			UBH_ConsumableLibrary::ReportRefusal(AvatarPawn, ActiveItemClass, Refusal);
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}

		if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
		{
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}

		// Exactly one item leaves the inventory, now. A cancel before the effect lands gives it back (see EndAbility).
		bItemConsumed = UBH_LootLibrary::RemoveItemFromPlayer(PlayerState, ActiveItemClass, 1) >= 1;
		if (!bItemConsumed)
		{
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}
	}

	const float UseSeconds = FMath::Max(bHaveDefinition ? ActiveDefinition.UseDuration : 1.f, 0.1f);

	if (bHaveDefinition)
	{
		UAnimMontage* Montage = ActiveDefinition.Montage.LoadSynchronous();
		if (Montage && Montage->GetPlayLength() > KINDA_SMALL_NUMBER)
		{
			// A montage auto-blends out over its last BlendOut seconds, so at a rate of Length / UseSeconds the pose is already fading
			// for the last BlendOut seconds of the use (the client drink looked over at ~0.86 s of a 1.2 s use). Pick the rate so the
			// blend-out STARTS when the use ends (the fade then plays over the effect landing), capped so the whole clip never exceeds
			// UseSeconds + BlendOut.
			const float Length = Montage->GetPlayLength();
			const float BlendOut = FMath::Clamp(Montage->GetDefaultBlendOutTime(), 0.f, Length * 0.5f);
			const float RateWholeClip = Length / (UseSeconds + BlendOut);
			const float RateBlendAtEnd = FMath::Max(Length - BlendOut, 0.05f) / UseSeconds;
			const float PlayRate = FMath::Clamp(FMath::Min(RateWholeClip, RateBlendAtEnd), 0.1f, 5.f);
			UE_LOG(LogBHUseConsumable, Log, TEXT("UseConsumable: montage '%s' length %.2f s, blend-out %.2f s, use %.2f s -> play rate %.3f (%s)."),
				*GetNameSafe(Montage), Length, BlendOut, UseSeconds, PlayRate, bAuthority ? TEXT("server") : TEXT("client"));

			MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this, NAME_None, Montage, PlayRate);
			MontageTask->OnInterrupted.AddDynamic(this, &UBH_GA_UseConsumable::OnMontageInterrupted);
			MontageTask->OnCancelled.AddDynamic(this, &UBH_GA_UseConsumable::OnMontageInterrupted);
			MontageTask->ReadyForActivation();
		}
		else
		{
			UE_LOG(LogBHUseConsumable, Verbose, TEXT("UseConsumable: no montage for '%s' (placeholder timing only)."), *GetNameSafe(ActiveItemClass));
		}
	}

	DelayTask = UAbilityTask_WaitDelay::WaitDelay(this, UseSeconds);
	DelayTask->OnFinish.AddDynamic(this, &UBH_GA_UseConsumable::OnUseTimeElapsed);
	DelayTask->ReadyForActivation();
}

void UBH_GA_UseConsumable::OnUseTimeElapsed()
{
	if (HasAuthority(&CurrentActivationInfo))
	{
		AActor* Avatar = GetAvatarActorFromActorInfo();
		const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
		const bool bDead = ASC && ASC->HasMatchingGameplayTag(TAG_State_Combat_Dead);
		if (Avatar && !bDead)
		{
			bEffectApplied = true; // set first: the item is spent from here on, even if the effect itself finds nothing to do
			UBH_ConsumableLibrary::ApplyConsumableEffect(Avatar, ActiveDefinition);
		}
		else
		{
			bEffectApplied = true; // dead: no effect, and no refund either
		}
	}
	FinishUse(/*bWasCancelled*/ false);
}

void UBH_GA_UseConsumable::OnMontageInterrupted()
{
	// Something took the slot before the use finished. The ability goes with it (refund happens in EndAbility).
	if (!bEffectApplied)
	{
		FinishUse(/*bWasCancelled*/ true);
	}
}

void UBH_GA_UseConsumable::FinishUse(bool bWasCancelled)
{
	const bool bAuthority = HasAuthority(&CurrentActivationInfo);
	// Only the server's end is replicated; the owning client ends its montage-only copy locally.
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, /*bReplicateEndAbility*/ bAuthority, bWasCancelled);
}

void UBH_GA_UseConsumable::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	// Cancelled before the effect landed: give the one item back (server only, silently, never to a dead player).
	if (bItemConsumed && !bEffectApplied && HasAuthority(&ActivationInfo))
	{
		const APawn* AvatarPawn = ActorInfo ? Cast<APawn>(ActorInfo->AvatarActor.Get()) : nullptr;
		APlayerState* PlayerState = AvatarPawn ? AvatarPawn->GetPlayerState() : nullptr;
		const UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
		const bool bDead = ASC && ASC->HasMatchingGameplayTag(TAG_State_Combat_Dead);
		if (PlayerState && ActiveItemClass && !bDead)
		{
			UBH_LootLibrary::GrantItem(PlayerState, ActiveItemClass, 1, /*bNotifyPlayer*/ false);
			UE_LOG(LogBHUseConsumable, Log, TEXT("UseConsumable: interrupted, refunded one %s to %s."), *GetNameSafe(ActiveItemClass), *GetNameSafe(AvatarPawn));
		}
	}
	bItemConsumed = false;
	bEffectApplied = false;

	if (MontageTask)
	{
		MontageTask->OnInterrupted.RemoveAll(this);
		MontageTask->OnCancelled.RemoveAll(this);
		MontageTask = nullptr;
	}
	if (DelayTask)
	{
		DelayTask->OnFinish.RemoveAll(this);
		DelayTask = nullptr;
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
