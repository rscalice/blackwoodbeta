// Blackwood Hollow - Hold-to-block ability (implementation)

#include "AbilitySystem/Abilities/AH_GA_Block.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystem/BH_CombatFunctionLibrary.h"
#include "AbilitySystem/Effects/AH_GE_CombatEffects.h"
#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Animation/AnimMontage.h"
#include "GameFramework/Actor.h"

namespace AH_GA_Block_Private
{
	static FName StartSection()  { return FName(TEXT("Start")); }
	static FName LoopSection()   { return FName(TEXT("Loop")); }
	static FName ImpactSection() { return FName(TEXT("Impact")); }
	static FName EndSection()    { return FName(TEXT("End")); }
}

UAH_GA_Block::UAH_GA_Block()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

	FGameplayTagContainer DefaultAssetTags;
	DefaultAssetTags.AddTag(TAG_Ability_Combat_Block);
	SetAssetTags(DefaultAssetTags);

	ActivationOwnedTags.AddTag(TAG_State_Combat_Blocking);

	ActivationBlockedTags.AddTag(TAG_State_Combat_Staggered);
	ActivationBlockedTags.AddTag(TAG_State_Combat_PostureBroken);
	ActivationBlockedTags.AddTag(TAG_State_Combat_Dead);

	// Raising the guard cancels your swing; you can't swing while guarding.
	CancelAbilitiesWithTag.AddTag(TAG_Ability_Combat_MeleeAttack);
	BlockAbilitiesWithTag.AddTag(TAG_Ability_Combat_MeleeAttack);
}

UAH_GA_Block* UAH_GA_Block::FindActiveBlock(UAbilitySystemComponent* ASC)
{
	if (!ASC)
	{
		return nullptr;
	}

	for (FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (Spec.IsActive() && Spec.Ability && Spec.Ability->IsA<UAH_GA_Block>())
		{
			if (UAH_GA_Block* Instance = Cast<UAH_GA_Block>(Spec.GetPrimaryInstance()))
			{
				return Instance;
			}
			return Cast<UAH_GA_Block>(Spec.Ability);
		}
	}
	return nullptr;
}

bool UAH_GA_Block::IsAttackInBlockArc(const AActor* Attacker) const
{
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Avatar || !Attacker)
	{
		return true;
	}

	const FVector ToAttacker = (Attacker->GetActorLocation() - Avatar->GetActorLocation()).GetSafeNormal2D();
	if (ToAttacker.IsNearlyZero())
	{
		return true;
	}

	const float CosAngle = FVector::DotProduct(Avatar->GetActorForwardVector().GetSafeNormal2D(), ToAttacker);
	const float AngleDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(CosAngle, -1.f, 1.f)));
	return AngleDegrees <= BlockAngleDegrees * 0.5f;
}

bool UAH_GA_Block::MontageHasSection(FName SectionName) const
{
	return ActiveGuardMontage && ActiveGuardMontage->IsValidSectionName(SectionName);
}

void UAH_GA_Block::SetSectionLink(FName From, FName To) const
{
	if (!MontageHasSection(From))
	{
		return;
	}
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		if (ASC->GetCurrentMontage() == ActiveGuardMontage)
		{
			ASC->CurrentMontageSetNextSectionName(From, MontageHasSection(To) ? To : NAME_None);
		}
	}
}

void UAH_GA_Block::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	using namespace AH_GA_Block_Private;

	// Stance-specific guard (e.g. both hands on the greatsword hilt); SnS / DS and anything unlisted use GuardMontage.
	ActiveGuardMontage = GuardMontage;
	{
		const FString Stance = UBH_CombatFunctionLibrary::GetCurrentOverlayPoseDisplayName(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr);
		if (!Stance.IsEmpty())
		{
			if (const TObjectPtr<UAnimMontage>* Found = StanceGuardMontages.Find(FName(*Stance)))
			{
				if (*Found)
				{
					ActiveGuardMontage = *Found;
				}
			}
		}
	}

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	UAbilityTask_WaitGameplayEvent* ImpactTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, TAG_Event_Combat_BlockImpact, nullptr, false, true);
	ImpactTask->EventReceived.AddDynamic(this, &UAH_GA_Block::OnBlockImpact);
	ImpactTask->ReadyForActivation();

	if (ActiveGuardMontage)
	{
		const FName FirstSection = MontageHasSection(StartSection()) ? StartSection()
			: (MontageHasSection(LoopSection()) ? LoopSection() : NAME_None);

		// bStopWhenAbilityEnds = false so the "End" (lower guard) section can finish after release.
		MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			this, NAME_None, ActiveGuardMontage, MontagePlayRate, FirstSection, /*bStopWhenAbilityEnds*/ false);
		MontageTask->OnInterrupted.AddDynamic(this, &UAH_GA_Block::OnMontageInterrupted);
		MontageTask->OnCancelled.AddDynamic(this, &UAH_GA_Block::OnMontageInterrupted);
		MontageTask->ReadyForActivation();

		SetSectionLink(StartSection(), LoopSection());
		SetSectionLink(LoopSection(), LoopSection());
		SetSectionLink(ImpactSection(), LoopSection());
		SetSectionLink(EndSection(), NAME_None);
	}
}

void UAH_GA_Block::OnBlockImpact(FGameplayEventData Payload)
{
	using namespace AH_GA_Block_Private;

	if (MontageHasSection(ImpactSection()))
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			if (ASC->GetCurrentMontage() == ActiveGuardMontage)
			{
				ASC->CurrentMontageJumpToSection(ImpactSection());
				SetSectionLink(ImpactSection(), LoopSection());
			}
		}
	}

	// Stamina drain + guard break. The impact event is sent by the server (UAH_AttributeSet), so this is authority-only.
	if (K2_HasAuthority())
	{
		DrainStaminaForBlock(Payload.EventMagnitude);
	}

	K2_OnBlockImpact(const_cast<AActor*>(Payload.Instigator.Get()), Payload.EventMagnitude);
}

void UAH_GA_Block::DrainStaminaForBlock(float PostureCost)
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	const float StaminaDrain = PostureCost * BlockStaminaScale;
	if (!ASC || StaminaDrain <= 0.f)
	{
		return;
	}

	UBH_CombatFunctionLibrary::ApplyStaminaCost(ASC, StaminaDrain);

	if (ASC->GetNumericAttribute(UAH_AttributeSet::GetStaminaAttribute()) > 0.f)
	{
		return;
	}

	// Guard break: out of stamina while blocking. Take whatever Posture is left so the standard posture-break path
	// (State.Combat.PostureBroken, GC_PostureBroken, UAH_GA_PostureBreak) runs, then lower the guard.
	const float RemainingPosture = ASC->GetNumericAttribute(UAH_AttributeSet::GetPostureAttribute());
	if (RemainingPosture > 0.f)
	{
		FGameplayEffectSpecHandle BreakSpec = MakeOutgoingGameplayEffectSpec(UAH_GE_PostureDamage::StaticClass(), GetAbilityLevel());
		if (BreakSpec.IsValid())
		{
			BreakSpec.Data->SetSetByCallerMagnitude(TAG_Data_PostureDamage, -RemainingPosture);
			ASC->ApplyGameplayEffectSpecToSelf(*BreakSpec.Data.Get());
		}
	}

	if (IsActive())
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
	}
}

void UAH_GA_Block::OnMontageInterrupted()
{
	// Another montage took over the body (hit, posture break...): the guard is down.
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}

void UAH_GA_Block::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	using namespace AH_GA_Block_Private;

	if (MontageTask)
	{
		MontageTask->OnInterrupted.RemoveAll(this);
		MontageTask->OnCancelled.RemoveAll(this);
		MontageTask = nullptr;
	}

	// Lower the guard: play "End" if the guard montage is still up, otherwise just stop it.
	if (UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr)
	{
		if (ActiveGuardMontage && ASC->GetCurrentMontage() == ActiveGuardMontage)
		{
			if (MontageHasSection(EndSection()))
			{
				ASC->CurrentMontageJumpToSection(EndSection());
				ASC->CurrentMontageSetNextSectionName(EndSection(), NAME_None);
			}
			else
			{
				ASC->CurrentMontageStop();
			}
		}
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
