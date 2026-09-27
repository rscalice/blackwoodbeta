// Blackwood Hollow - Melee combo attack ability base (implementation)

#include "AbilitySystem/Abilities/AH_GA_MeleeAttack_Base.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystem/Effects/AH_GE_CombatEffects.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Animation/AnimMontage.h"

UAH_GA_MeleeAttack_Base::UAH_GA_MeleeAttack_Base()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

	FGameplayTagContainer DefaultAssetTags;
	DefaultAssetTags.AddTag(TAG_Ability_Combat_MeleeAttack);
	SetAssetTags(DefaultAssetTags);

	ActivationOwnedTags.AddTag(TAG_State_Combat_Attacking);

	ActivationBlockedTags.AddTag(TAG_State_Combat_Staggered);
	ActivationBlockedTags.AddTag(TAG_State_Combat_PostureBroken);
	ActivationBlockedTags.AddTag(TAG_State_Combat_Dead);
	ActivationBlockedTags.AddTag(TAG_State_Combat_Parrying);

	DamageEffectClass = UAH_GE_MeleeDamage::StaticClass();
	PostureDamageEffectClass = UAH_GE_PostureDamage::StaticClass();

	ComboSectionNames = { FName(TEXT("Attack1")), FName(TEXT("Attack2")), FName(TEXT("Attack3")) };
}

// ============================================================================
// Activation / end
// ============================================================================

void UAH_GA_MeleeAttack_Base::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (!AttackMontage || ComboSectionNames.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: AttackMontage / ComboSectionNames not set."), *GetName());
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	LocalComboStep = 0;
	bComboWindowOpen = false;
	bInputBuffered = false;
	bWindowClosedThisStep = false;
	bIgnoreNextWindowClose = false;

	const FName StartSection = ComboSectionNames[0];

	MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this, NAME_None, AttackMontage, MontagePlayRate, StartSection);
	MontageTask->OnCompleted.AddDynamic(this, &UAH_GA_MeleeAttack_Base::OnMontageCompleted);
	MontageTask->OnBlendOut.AddDynamic(this, &UAH_GA_MeleeAttack_Base::OnMontageBlendOut);
	MontageTask->OnInterrupted.AddDynamic(this, &UAH_GA_MeleeAttack_Base::OnMontageInterrupted);
	MontageTask->OnCancelled.AddDynamic(this, &UAH_GA_MeleeAttack_Base::OnMontageCancelled);
	MontageTask->ReadyForActivation();

	UnlinkSection(StartSection);

	// Owner-side events from UANS_ComboWindow / UANS_MeleeHitbox / input / parry.
	UAbilityTask_WaitGameplayEvent* WindowOpenTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, TAG_Event_Combat_ComboWindow_Open, nullptr, false, true);
	WindowOpenTask->EventReceived.AddDynamic(this, &UAH_GA_MeleeAttack_Base::OnComboWindowOpened);
	WindowOpenTask->ReadyForActivation();

	UAbilityTask_WaitGameplayEvent* WindowCloseTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, TAG_Event_Combat_ComboWindow_Close, nullptr, false, true);
	WindowCloseTask->EventReceived.AddDynamic(this, &UAH_GA_MeleeAttack_Base::OnComboWindowClosed);
	WindowCloseTask->ReadyForActivation();

	UAbilityTask_WaitGameplayEvent* InputTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, TAG_Event_Combat_Input_Attack, nullptr, false, true);
	InputTask->EventReceived.AddDynamic(this, &UAH_GA_MeleeAttack_Base::OnAttackInput);
	InputTask->ReadyForActivation();

	UAbilityTask_WaitGameplayEvent* HitTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, TAG_Event_Combat_HitDealt, nullptr, false, true);
	HitTask->EventReceived.AddDynamic(this, &UAH_GA_MeleeAttack_Base::OnHitDealt);
	HitTask->ReadyForActivation();

	UAbilityTask_WaitGameplayEvent* ParriedTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, TAG_Event_Combat_Parry_Success, nullptr, false, true);
	ParriedTask->EventReceived.AddDynamic(this, &UAH_GA_MeleeAttack_Base::OnParried);
	ParriedTask->ReadyForActivation();

	K2_OnComboStepStarted(0, StartSection);
}

void UAH_GA_MeleeAttack_Base::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (ActorInfo)
	{
		if (UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get())
		{
			ASC->SetLooseGameplayTagCount(TAG_State_Combat_ComboWindow, 0);
		}
	}

	MontageTask = nullptr;
	LocalComboStep = 0;
	bComboWindowOpen = false;
	bInputBuffered = false;
	bWindowClosedThisStep = false;
	bIgnoreNextWindowClose = false;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

// ============================================================================
// Combo flow
// ============================================================================

int32 UAH_GA_MeleeAttack_Base::GetCurrentComboStep() const
{
	// The server never sees the client's input, only the replicated section
	// jump, so derive the step from what is actually playing when possible.
	if (const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		if (AttackMontage && ASC->GetCurrentMontage() == AttackMontage)
		{
			const int32 SectionIndex = ComboSectionNames.IndexOfByKey(ASC->GetCurrentMontageSectionName());
			if (SectionIndex != INDEX_NONE)
			{
				return SectionIndex;
			}
		}
	}
	return LocalComboStep;
}

void UAH_GA_MeleeAttack_Base::UnlinkSection(FName SectionName) const
{
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		if (ASC->GetCurrentMontage() == AttackMontage)
		{
			ASC->CurrentMontageSetNextSectionName(SectionName, NAME_None);
		}
	}
}

bool UAH_GA_MeleeAttack_Base::AdvanceCombo()
{
	int32 NextStep = LocalComboStep + 1;
	if (!ComboSectionNames.IsValidIndex(NextStep))
	{
		if (!bLoopCombo)
		{
			bInputBuffered = false;
			return false;
		}
		NextStep = 0;
	}

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC || ASC->GetCurrentMontage() != AttackMontage)
	{
		return false;
	}

	// If we jump while the current step's window is still open, that window's
	// NotifyEnd (Close event) arrives after the jump and belongs to the OLD step.
	bIgnoreNextWindowClose = bComboWindowOpen;

	LocalComboStep = NextStep;
	bInputBuffered = false;
	bComboWindowOpen = false;
	bWindowClosedThisStep = false;

	const FName SectionName = ComboSectionNames[NextStep];
	ASC->CurrentMontageJumpToSection(SectionName);
	UnlinkSection(SectionName);

	K2_OnComboStepStarted(NextStep, SectionName);
	return true;
}

void UAH_GA_MeleeAttack_Base::OnComboWindowOpened(FGameplayEventData Payload)
{
	bComboWindowOpen = true;
	bWindowClosedThisStep = false;

	if (bInputBuffered && bAdvanceImmediatelyInWindow)
	{
		AdvanceCombo();
	}
}

void UAH_GA_MeleeAttack_Base::OnComboWindowClosed(FGameplayEventData Payload)
{
	if (bIgnoreNextWindowClose)
	{
		bIgnoreNextWindowClose = false;
		return;
	}

	bComboWindowOpen = false;

	if (bInputBuffered)
	{
		AdvanceCombo();
	}
	else
	{
		// Window missed: further presses this step are too late.
		bWindowClosedThisStep = true;
	}
}

void UAH_GA_MeleeAttack_Base::OnAttackInput(FGameplayEventData Payload)
{
	if (bWindowClosedThisStep)
	{
		return;
	}

	if (bComboWindowOpen)
	{
		if (bAdvanceImmediatelyInWindow)
		{
			AdvanceCombo();
		}
		else
		{
			bInputBuffered = true;
		}
	}
	else if (bBufferInputBeforeWindow)
	{
		bInputBuffered = true;
	}
}

// ============================================================================
// Damage
// ============================================================================

float UAH_GA_MeleeAttack_Base::GetStepDamageMultiplier(int32 ComboStep) const
{
	return ComboStepDamageMultipliers.IsValidIndex(ComboStep) ? ComboStepDamageMultipliers[ComboStep] : 1.f;
}

float UAH_GA_MeleeAttack_Base::CalculateDamage_Implementation(AActor* Target, int32 ComboStep, float HitboxMultiplier) const
{
	float Damage = BaseDamage;

	if (bAddAttackPower)
	{
		if (const UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo())
		{
			Damage += SourceASC->GetNumericAttribute(UAH_AttributeSet::GetAttackPowerAttribute());
		}
	}

	Damage *= GetStepDamageMultiplier(ComboStep) * HitboxMultiplier;

	if (bSubtractTargetDefense)
	{
		if (const UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target))
		{
			Damage -= TargetASC->GetNumericAttribute(UAH_AttributeSet::GetDefenseAttribute());
		}
	}

	return FMath::Max(Damage, MinimumDamage);
}

void UAH_GA_MeleeAttack_Base::OnHitDealt(FGameplayEventData Payload)
{
	// Damage is server-authoritative; clients' own hitbox sweeps are ignored here.
	if (!K2_HasAuthority())
	{
		return;
	}

	AActor* Target = const_cast<AActor*>(Payload.Target.Get());
	UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target);
	UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo();
	if (!TargetASC || !SourceASC)
	{
		return;
	}

	// Parried: UAH_GA_Parry (on the target) already handled posture and will
	// send Event.Combat.Parry.Success back to us. UAH_AttributeSet also rejects
	// melee IncomingDamage while State.Combat.Parrying is present.
	if (TargetASC->HasMatchingGameplayTag(TAG_State_Combat_Parrying))
	{
		return;
	}

	const int32 ComboStep = GetCurrentComboStep();
	const float HitboxMultiplier = Payload.EventMagnitude > 0.f ? Payload.EventMagnitude : 1.f;
	const FHitResult HitResult = UAbilitySystemBlueprintLibrary::GetHitResultFromTargetData(Payload.TargetData, 0);

	float DamageApplied = 0.f;
	if (DamageEffectClass)
	{
		DamageApplied = CalculateDamage(Target, ComboStep, HitboxMultiplier);

		FGameplayEffectSpecHandle DamageSpec = MakeOutgoingGameplayEffectSpec(DamageEffectClass, GetAbilityLevel());
		if (DamageSpec.IsValid())
		{
			DamageSpec.Data->SetSetByCallerMagnitude(TAG_Data_Damage, DamageApplied);
			DamageSpec.Data->AddDynamicAssetTag(TAG_Damage_Type_Melee);
			DamageSpec.Data->GetContext().AddHitResult(HitResult, true);
			SourceASC->ApplyGameplayEffectSpecToTarget(*DamageSpec.Data.Get(), TargetASC);
		}
	}

	if (PostureDamageEffectClass && BasePostureDamage > 0.f)
	{
		const float PostureDamage = BasePostureDamage * GetStepDamageMultiplier(ComboStep) * HitboxMultiplier;

		FGameplayEffectSpecHandle PostureSpec = MakeOutgoingGameplayEffectSpec(PostureDamageEffectClass, GetAbilityLevel());
		if (PostureSpec.IsValid())
		{
			PostureSpec.Data->SetSetByCallerMagnitude(TAG_Data_PostureDamage, -PostureDamage);
			PostureSpec.Data->AddDynamicAssetTag(TAG_Damage_Type_Melee);
			SourceASC->ApplyGameplayEffectSpecToTarget(*PostureSpec.Data.Get(), TargetASC);
		}
	}

	K2_OnHitConfirmed(Target, HitResult, DamageApplied);
}

void UAH_GA_MeleeAttack_Base::OnParried(FGameplayEventData Payload)
{
	// Parry.Success goes to both parrier and attacker; only react if we were the attacker.
	if (Payload.Target.Get() != GetAvatarActorFromActorInfo())
	{
		return;
	}

	K2_OnAttackParried(const_cast<AActor*>(Payload.Instigator.Get()));

	if (bEndComboWhenParried)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
	}
}

// ============================================================================
// Montage callbacks
// ============================================================================

void UAH_GA_MeleeAttack_Base::OnMontageCompleted()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UAH_GA_MeleeAttack_Base::OnMontageBlendOut()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UAH_GA_MeleeAttack_Base::OnMontageInterrupted()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}

void UAH_GA_MeleeAttack_Base::OnMontageCancelled()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}
