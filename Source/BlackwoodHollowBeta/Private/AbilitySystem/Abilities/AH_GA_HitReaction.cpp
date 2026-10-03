// Blackwood Hollow - Hit reaction (flinch / stagger) ability (implementation)

#include "AbilitySystem/Abilities/AH_GA_HitReaction.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "Combat/BH_StanceComponent.h"
#include "AbilitySystem/BH_CombatFunctionLibrary.h"
#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Animation/AnimMontage.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "GameFramework/Actor.h"

UAH_GA_HitReaction::UAH_GA_HitReaction()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	// Triggered by a server-side event (UAH_AttributeSet), mirrored to the owning client.
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;
	// A new hit while already reacting restarts the reaction (and its montage).
	bRetriggerInstancedAbility = true;

	FGameplayTagContainer DefaultAssetTags;
	DefaultAssetTags.AddTag(TAG_Ability_Combat_HitReaction);
	SetAssetTags(DefaultAssetTags);

	ActivationOwnedTags.AddTag(TAG_State_Combat_Staggered);
	ActivationOwnedTags.AddTag(TAG_State_Combat_MovementLocked);

	// Posture break / death own the character's animation; blocked hits use Event.Combat.BlockImpact instead.
	ActivationBlockedTags.AddTag(TAG_State_Combat_PostureBroken);
	ActivationBlockedTags.AddTag(TAG_State_Combat_Dead);
	// Hyper armor (e.g. greatsword swing): damage still lands, but the attacker is not staggered / interrupted.
	ActivationBlockedTags.AddTag(TAG_State_Combat_HyperArmor);

	// Getting hit interrupts your own swing / parry.
	CancelAbilitiesWithTag.AddTag(TAG_Ability_Combat_MeleeAttack);
	CancelAbilitiesWithTag.AddTag(TAG_Ability_Combat_Parry);

	FAbilityTriggerData Trigger;
	Trigger.TriggerTag = TAG_Event_Combat_DamageReceived;
	Trigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	AbilityTriggers.Add(Trigger);
}

EBH_HitDirection UAH_GA_HitReaction::ComputeHitDirection(const AActor* Victim, const FVector& SourceLocation)
{
	if (!Victim)
	{
		return EBH_HitDirection::Front;
	}

	const FVector ToSource = (SourceLocation - Victim->GetActorLocation()).GetSafeNormal2D();
	if (ToSource.IsNearlyZero())
	{
		return EBH_HitDirection::Front;
	}

	const float ForwardDot = FVector::DotProduct(ToSource, Victim->GetActorForwardVector());
	const float RightDot = FVector::DotProduct(ToSource, Victim->GetActorRightVector());

	if (FMath::Abs(ForwardDot) >= FMath::Abs(RightDot))
	{
		return ForwardDot >= 0.f ? EBH_HitDirection::Front : EBH_HitDirection::Back;
	}
	return RightDot >= 0.f ? EBH_HitDirection::Right : EBH_HitDirection::Left;
}

bool UAH_GA_HitReaction::ShouldAbilityRespondToEvent(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayEventData* Payload) const
{
	if (!Super::ShouldAbilityRespondToEvent(ActorInfo, Payload))
	{
		return false;
	}
	return !Payload || Payload->EventMagnitude >= MinimumDamageToReact;
}

UAnimMontage* UAH_GA_HitReaction::GetStanceMontage(EBH_HitDirection Direction) const
{
	const AActor* StanceAvatar = GetAvatarActorFromActorInfo();
	const FGameplayTag StanceTag = UBH_StanceComponent::FindStanceComponent(StanceAvatar) ? UBH_StanceComponent::GetStanceTagOf(StanceAvatar) : FGameplayTag();
	const FBH_HitMontageSet* Set = StanceTag.IsValid() ? StanceHitMontagesByTag.Find(StanceTag) : nullptr;
	if (!Set)
	{
		const FString Stance = UBH_CombatFunctionLibrary::GetCurrentOverlayPoseDisplayName(StanceAvatar);
		Set = Stance.IsEmpty() ? nullptr : StanceHitMontages.Find(FName(*Stance));
	}
	if (!Set)
	{
		return nullptr;
	}
	UAnimMontage* Montage = nullptr;
	switch (Direction)
	{
	case EBH_HitDirection::Back:  Montage = Set->Back;  break;
	case EBH_HitDirection::Left:  Montage = Set->Left;  break;
	case EBH_HitDirection::Right: Montage = Set->Right; break;
	default: break;
	}
	return Montage ? Montage : Set->Front.Get();
}

UAnimMontage* UAH_GA_HitReaction::GetMontageForDirection(EBH_HitDirection Direction) const
{
	if (UAnimMontage* StanceMontage = GetStanceMontage(Direction))
	{
		return StanceMontage;
	}
	UAnimMontage* Montage = nullptr;
	switch (Direction)
	{
	case EBH_HitDirection::Back:  Montage = HitMontageBack;  break;
	case EBH_HitDirection::Left:  Montage = HitMontageLeft;  break;
	case EBH_HitDirection::Right: Montage = HitMontageRight; break;
	default: break;
	}
	return Montage ? Montage : HitMontageFront.Get();
}

void UAH_GA_HitReaction::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// A combat ability draws the weapon (authority only; replicates through the stance component).
	if (HasAuthority(&ActivationInfo))
	{
		if (UBH_StanceComponent* StanceComp = UBH_StanceComponent::FindStanceComponent(GetAvatarActorFromActorInfo()))
		{
			StanceComp->NotifyCombatActivity();
		}
	}

	AActor* Avatar = GetAvatarActorFromActorInfo();
	AActor* Attacker = TriggerEventData ? const_cast<AActor*>(TriggerEventData->Instigator.Get()) : nullptr;
	const float Damage = TriggerEventData ? TriggerEventData->EventMagnitude : 0.f;

	const EBH_HitDirection Direction = (Attacker && Avatar)
		? ComputeHitDirection(Avatar, Attacker->GetActorLocation())
		: EBH_HitDirection::Front;

	K2_OnHitReaction(Direction, Attacker, Damage);

	if (UAnimMontage* Montage = GetMontageForDirection(Direction))
	{
		MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this, NAME_None, Montage, MontagePlayRate);
		MontageTask->OnCompleted.AddDynamic(this, &UAH_GA_HitReaction::OnMontageFinished);
		MontageTask->OnBlendOut.AddDynamic(this, &UAH_GA_HitReaction::OnMontageFinished);
		MontageTask->OnInterrupted.AddDynamic(this, &UAH_GA_HitReaction::OnMontageInterrupted);
		MontageTask->OnCancelled.AddDynamic(this, &UAH_GA_HitReaction::OnMontageInterrupted);
		MontageTask->ReadyForActivation();
	}
	else if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(FallbackTimerHandle, this, &UAH_GA_HitReaction::FinishReaction, FallbackStaggerDuration, false);
	}
	else
	{
		FinishReaction();
	}
}

void UAH_GA_HitReaction::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FallbackTimerHandle);
	}
	MontageTask = nullptr;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UAH_GA_HitReaction::FinishReaction()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UAH_GA_HitReaction::OnMontageFinished()
{
	FinishReaction();
}

void UAH_GA_HitReaction::OnMontageInterrupted()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}
