// Blackwood Hollow - Posture break (guard break / deathblow window) ability (implementation)

#include "AbilitySystem/Abilities/AH_GA_PostureBreak.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystem/BH_CombatFunctionLibrary.h"
#include "AbilitySystem/Effects/AH_GE_CombatEffects.h"
#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Animation/AnimMontage.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

UAH_GA_PostureBreak::UAH_GA_PostureBreak()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;

	FGameplayTagContainer DefaultAssetTags;
	DefaultAssetTags.AddTag(TAG_Ability_Combat_PostureBreak);
	SetAssetTags(DefaultAssetTags);

	// NOTE: not blocked by State.Combat.PostureBroken - UAH_AttributeSet adds that
	// tag right before sending the trigger event.
	ActivationBlockedTags.AddTag(TAG_State_Combat_Dead);

	CancelAbilitiesWithTag.AddTag(TAG_Ability_Combat_MeleeAttack);
	CancelAbilitiesWithTag.AddTag(TAG_Ability_Combat_Parry);
	CancelAbilitiesWithTag.AddTag(TAG_Ability_Combat_Block);
	CancelAbilitiesWithTag.AddTag(TAG_Ability_Combat_HitReaction);

	PostureEffectClass = UAH_GE_PostureDamage::StaticClass();

	FAbilityTriggerData Trigger;
	Trigger.TriggerTag = TAG_Event_Combat_PostureBreak;
	Trigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	AbilityTriggers.Add(Trigger);
}

void UAH_GA_PostureBreak::SetMovementDisabled(bool bDisabled)
{
	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Movement)
	{
		return;
	}

	if (bDisabled)
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
		bMovementDisabledByUs = true;
	}
	else if (bMovementDisabledByUs)
	{
		Movement->SetDefaultMovementMode();
		bMovementDisabledByUs = false;
	}
}

void UAH_GA_PostureBreak::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// The attribute set only tags the server; make sure the owning client has it too.
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->SetLooseGameplayTagCount(TAG_State_Combat_PostureBroken, 1);
	}

	if (bDisableMovement)
	{
		SetMovementDisabled(true);
	}

	K2_OnPostureBroken(TriggerEventData ? const_cast<AActor*>(TriggerEventData->Instigator.Get()) : nullptr);

	UAnimMontage* BreakMontage = PostureBreakMontage;
	{
		const FString Stance = UBH_CombatFunctionLibrary::GetCurrentOverlayPoseDisplayName(GetAvatarActorFromActorInfo());
		if (!Stance.IsEmpty())
		{
			if (const TObjectPtr<UAnimMontage>* Found = StancePostureBreakMontages.Find(FName(*Stance)))
			{
				if (*Found)
				{
					BreakMontage = *Found;
				}
			}
		}
	}

	if (BreakMontage)
	{
		MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this, NAME_None, BreakMontage, MontagePlayRate);
		// Montage finishing early doesn't end the break - BreakDuration does.
		MontageTask->OnInterrupted.AddDynamic(this, &UAH_GA_PostureBreak::OnMontageInterrupted);
		MontageTask->OnCancelled.AddDynamic(this, &UAH_GA_PostureBreak::OnMontageInterrupted);
		MontageTask->ReadyForActivation();
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(RecoverTimerHandle, this, &UAH_GA_PostureBreak::Recover, BreakDuration, false);
	}
	else
	{
		Recover();
	}
}

void UAH_GA_PostureBreak::Recover()
{
	// Refill posture (authority only; replicates through the attribute set).
	if (K2_HasAuthority() && PostureEffectClass && RecoveredPosturePercent > 0.f)
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			const float MaxPosture = ASC->GetNumericAttribute(UAH_AttributeSet::GetMaxPostureAttribute());
			const float Current = ASC->GetNumericAttribute(UAH_AttributeSet::GetPostureAttribute());
			const float Delta = FMath::Max(0.f, MaxPosture * RecoveredPosturePercent - Current);

			FGameplayEffectSpecHandle Spec = MakeOutgoingGameplayEffectSpec(PostureEffectClass, GetAbilityLevel());
			if (Spec.IsValid() && Delta > 0.f)
			{
				Spec.Data->SetSetByCallerMagnitude(TAG_Data_PostureDamage, Delta);
				ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
			}
		}
	}

	K2_OnPostureRecovered();
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UAH_GA_PostureBreak::OnMontageInterrupted()
{
	// Something stronger (death, a scripted montage) took over the body. Keep the
	// break timer running so the vulnerable window isn't cut short.
}

void UAH_GA_PostureBreak::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RecoverTimerHandle);
	}

	SetMovementDisabled(false);

	if (UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr)
	{
		ASC->SetLooseGameplayTagCount(TAG_State_Combat_PostureBroken, 0);
	}

	MontageTask = nullptr;
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
