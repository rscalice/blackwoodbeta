// Blackwood Hollow - Melee combo attack ability base (implementation)

#include "AbilitySystem/Abilities/AH_GA_MeleeAttack_Base.h"
#include "AbilitySystem/Abilities/AH_GA_Block.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "Combat/BH_StanceComponent.h"
#include "Combat/BH_CombatIdentityComponent.h"
#include "AbilitySystem/BH_CombatFunctionLibrary.h"
#include "AbilitySystem/Effects/AH_GE_CombatEffects.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "GameplayEffectTypes.h"
#include "GameplayEffect.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Animation/AnimMontage.h"
#include "Combat/BH_LockOnComponent.h"
#include "Combat/BH_CombatFeel.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"

UAH_GA_MeleeAttack_Base::UAH_GA_MeleeAttack_Base()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

	FGameplayTagContainer DefaultAssetTags;
	DefaultAssetTags.AddTag(TAG_Ability_Combat_MeleeAttack);
	SetAssetTags(DefaultAssetTags);

	ActivationOwnedTags.AddTag(TAG_State_Combat_Attacking);
	ActivationOwnedTags.AddTag(TAG_State_Combat_MovementLocked);

	ActivationBlockedTags.AddTag(TAG_State_Combat_Staggered);
	ActivationBlockedTags.AddTag(TAG_State_Combat_PostureBroken);
	ActivationBlockedTags.AddTag(TAG_State_Combat_Dead);
	ActivationBlockedTags.AddTag(TAG_State_Combat_Parrying);

	// Souls feel: a swing may start on a sliver of stamina (it floors at 0), but the combo won't advance on empty.
	StaminaCost = 12.f;
	bAllowStaminaOvercommit = true;

	DamageEffectClass = UAH_GE_Damage_Formula::StaticClass();
	PostureDamageEffectClass = UAH_GE_PostureDamage::StaticClass();
	HitCueTag = TAG_GameplayCue_Combat_Hit;

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

	// A combat ability draws the weapon (authority only; replicates through the stance component).
	if (HasAuthority(&ActivationInfo))
	{
		if (UBH_StanceComponent* StanceComp = UBH_StanceComponent::FindStanceComponent(GetAvatarActorFromActorInfo()))
		{
			StanceComp->NotifyCombatActivity();
		}
	}

	// The server computes its own target here (activation reaches it with the same movement state); no RPC needed.
	FaceMeleeTarget(false);

	LocalComboStep = 0;
	StaminaChargedStep = 0; // step 0 was paid by CommitAbility
	bComboWindowOpen = false;
	bInputBuffered = false;
	bWindowClosedThisStep = false;
	bIgnoreNextWindowClose = false;
	bInRecoil = false;

	if (bHyperArmorDuringSwing && !bHyperArmorGranted)
	{
		if (UAbilitySystemComponent* ArmorASC = GetAbilitySystemComponentFromActorInfo())
		{
			ArmorASC->AddLooseGameplayTag(TAG_State_Combat_HyperArmor);
			bHyperArmorGranted = true;
		}
	}

	const FName StartSection = ComboSectionNames[0];

	MontageTask = StartMontageTask(AttackMontage, GetEffectivePlayRate(MontagePlayRate), StartSection);

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
			if (bHyperArmorGranted)
			{
				ASC->RemoveLooseGameplayTag(TAG_State_Combat_HyperArmor);
			}
		}
	}
	bHyperArmorGranted = false;

	// Give the pawn back to the camera yaw (a soft-lock turn suspends controller rotation for the swing).
	if (ActorInfo)
	{
		const APawn* AvatarPawn = Cast<APawn>(ActorInfo->AvatarActor.Get());
		const AController* Controller = AvatarPawn ? AvatarPawn->GetController() : nullptr;
		if (UBH_LockOnComponent* LockOn = Controller ? Controller->FindComponentByClass<UBH_LockOnComponent>() : nullptr)
		{
			LockOn->EndMeleeFacingOverride();
		}
	}

	MontageTask = nullptr;
	LocalComboStep = 0;
	bComboWindowOpen = false;
	bInputBuffered = false;
	bWindowClosedThisStep = false;
	bIgnoreNextWindowClose = false;
	bInRecoil = false;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UAH_GA_MeleeAttack_Base::ApplyCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo) const
{
	if (!ComboStepStaminaCosts.IsValidIndex(0))
	{
		Super::ApplyCost(Handle, ActorInfo, ActivationInfo);
		return;
	}

	// Per-step cost array: step 0 pays ComboStepStaminaCosts[0] instead of StaminaCost.
	UGameplayAbility::ApplyCost(Handle, ActorInfo, ActivationInfo);
	const float Cost = GetStepStaminaCost(0);
	if (Cost > 0.f && ActorInfo && HasAuthorityOrPredictionKey(ActorInfo, &ActivationInfo))
	{
		UBH_CombatFunctionLibrary::ApplyStaminaCost(ActorInfo->AbilitySystemComponent.Get(), Cost);
	}
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

void UAH_GA_MeleeAttack_Base::FaceMeleeTarget(bool bNotifyServer)
{
	if (!bFaceLockedTargetOnActivate && !bFaceSoftTargetOnActivate)
	{
		return;
	}
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	const APawn* AvatarPawn = Cast<APawn>(Avatar);
	const AController* Controller = AvatarPawn ? AvatarPawn->GetController() : nullptr;
	if (UBH_LockOnComponent* LockOn = Controller ? Controller->FindComponentByClass<UBH_LockOnComponent>() : nullptr)
	{
		bool bUsedSoftTarget = false;
		LockOn->FaceMeleeTarget(bFaceLockedTargetOnActivate, bFaceSoftTargetOnActivate, bNotifyServer, bUsedSoftTarget);
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

	// Out of stamina: the combo stops here (the montage was un-linked, so it ends after the current step).
	if (!CanAffordStamina())
	{
		bInputBuffered = false;
		return false;
	}

	// If we jump while the current step's window is still open, that window's
	// NotifyEnd (Close event) arrives after the jump and belongs to the OLD step.
	bIgnoreNextWindowClose = bComboWindowOpen;

	LocalComboStep = NextStep;
	bInputBuffered = false;
	bComboWindowOpen = false;
	bWindowClosedThisStep = false;

	// Re-aim for the new step. The server never sees this input for a remote client, so the client sends its target.
	FaceMeleeTarget(true);

	if (K2_HasAuthority())
	{
		if (NextStep == 0)
		{
			StaminaChargedStep = -1; // a wrapped (looping) combo pays again for step 0
		}
		ChargeStaminaForStep(NextStep);
	}

	const FName SectionName = ComboSectionNames[NextStep];
	if (bCrossfadeComboSteps)
	{
		CrossfadeToSection(SectionName);
	}
	else
	{
		// Legacy hard jump inside the running montage instance; still refresh the rate (AttackSpeed may have changed).
		ASC->CurrentMontageSetPlayRate(GetEffectivePlayRate(MontagePlayRate));
		ASC->CurrentMontageJumpToSection(SectionName);
	}
	UnlinkSection(SectionName);

	K2_OnComboStepStarted(NextStep, SectionName);
	return true;
}

void UAH_GA_MeleeAttack_Base::ChargeStaminaForStep(int32 Step)
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	while (ASC && StaminaChargedStep < Step)
	{
		++StaminaChargedStep;
		UBH_CombatFunctionLibrary::ApplyStaminaCost(ASC, GetStepStaminaCost(StaminaChargedStep));
	}
}

void UAH_GA_MeleeAttack_Base::OnComboWindowOpened(FGameplayEventData Payload)
{
	// Server-side catch-up for a remote client's combo steps (see ChargeStaminaForStep).
	if (K2_HasAuthority() && !bInRecoil)
	{
		ChargeStaminaForStep(GetCurrentComboStep());
	}

	if (bInRecoil)
	{
		return;
	}

	bComboWindowOpen = true;
	bWindowClosedThisStep = false;

	if (bInputBuffered && bAdvanceImmediatelyInWindow)
	{
		AdvanceCombo();
	}
}

void UAH_GA_MeleeAttack_Base::OnComboWindowClosed(FGameplayEventData Payload)
{
	if (bInRecoil)
	{
		return;
	}

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
	if (bInRecoil)
	{
		return;
	}

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

float UAH_GA_MeleeAttack_Base::GetStepPostureMultiplier(int32 ComboStep) const
{
	return ComboStepPostureMultipliers.IsValidIndex(ComboStep) ? ComboStepPostureMultipliers[ComboStep] : GetStepDamageMultiplier(ComboStep);
}

float UAH_GA_MeleeAttack_Base::GetStepStaminaCost(int32 ComboStep) const
{
	return ComboStepStaminaCosts.IsValidIndex(ComboStep) ? ComboStepStaminaCosts[ComboStep] : StaminaCost;
}

float UAH_GA_MeleeAttack_Base::GetEffectivePlayRate(float BaseRate) const
{
	float Speed = 1.f;
	if (const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		if (ASC->HasAttributeSetForAttribute(UAH_AttributeSet::GetAttackSpeedAttribute()))
		{
			Speed = ASC->GetNumericAttribute(UAH_AttributeSet::GetAttackSpeedAttribute());
		}
	}
	return FMath::Max(BaseRate * Speed, 0.1f);
}

float UAH_GA_MeleeAttack_Base::GetDamageMultiplier(int32 ComboStep, float HitboxMultiplier, bool bRiposte) const
{
	// Per-pawn scale (enemy archetypes sharing player combos) is part of the multiplier; Defense is applied by the formula.
	float Multiplier = GetStepDamageMultiplier(ComboStep) * HitboxMultiplier
		* UBH_CombatIdentityComponent::GetOutgoingCombatMultiplier(GetAvatarActorFromActorInfo());

	// Riposte: the first hit after a perfect parry hits harder (OnHitDealt consumes the window afterwards).
	if (bRiposte)
	{
		Multiplier *= RiposteDamageMultiplier;
	}
	return Multiplier;
}

float UAH_GA_MeleeAttack_Base::CalculateDamage_Implementation(AActor* Target, int32 ComboStep, float HitboxMultiplier) const
{
	const UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo();

	float AttackPower = 0.f;
	if (SourceASC && SourceASC->HasAttributeSetForAttribute(UAH_AttributeSet::GetAttackPowerAttribute()))
	{
		AttackPower = SourceASC->GetNumericAttribute(UAH_AttributeSet::GetAttackPowerAttribute());
	}

	float Defense = 0.f;
	if (const UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target))
	{
		if (TargetASC->HasAttributeSetForAttribute(UAH_AttributeSet::GetDefenseAttribute()))
		{
			Defense = TargetASC->GetNumericAttribute(UAH_AttributeSet::GetDefenseAttribute());
		}
	}

	const bool bRiposte = SourceASC && SourceASC->HasMatchingGameplayTag(TAG_State_Combat_RiposteReady);
	return UBH_CombatFunctionLibrary::ComputeDamage(BaseDamage, AttackPower, bAddAttackPower ? 1.f : 0.f,
		GetDamageMultiplier(ComboStep, HitboxMultiplier, bRiposte), Defense);
}

void UAH_GA_MeleeAttack_Base::OnHitDealt(FGameplayEventData Payload)
{
	if (bInRecoil)
	{
		return;
	}

	// Damage is server-authoritative; clients' own hitbox sweeps are ignored here.
	if (!K2_HasAuthority())
	{
		return;
	}

	ChargeStaminaForStep(GetCurrentComboStep());

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

	// Blocked: UAH_AttributeSet converts the hit into chip damage + the block's
	// own posture cost, so the attack's direct posture damage is skipped below.
	// Evaluated before damage is applied (a posture break would drop the guard).
	bool bBlockedByTarget = false;
	if (TargetASC->HasMatchingGameplayTag(TAG_State_Combat_Blocking))
	{
		if (const UAH_GA_Block* ActiveBlock = UAH_GA_Block::FindActiveBlock(TargetASC))
		{
			bBlockedByTarget = ActiveBlock->IsAttackInBlockArc(GetAvatarActorFromActorInfo());
		}
	}

	// Evaluated once, before damage: CalculateDamage reads the same tag.
	const bool bRiposte = SourceASC->HasMatchingGameplayTag(TAG_State_Combat_RiposteReady);

	const int32 ComboStep = GetCurrentComboStep();
	const float HitboxMultiplier = Payload.EventMagnitude > 0.f ? Payload.EventMagnitude : 1.f;
	const FHitResult HitResult = UAbilitySystemBlueprintLibrary::GetHitResultFromTargetData(Payload.TargetData, 0);

	float DamageApplied = 0.f;
	if (DamageEffectClass)
	{
		// DamageApplied is the real number for hit-feel (same inputs as the GE); the formula GE gets the raw inputs instead.
		DamageApplied = CalculateDamage(Target, ComboStep, HitboxMultiplier);

		FGameplayEffectSpecHandle DamageSpec = MakeOutgoingGameplayEffectSpec(DamageEffectClass, GetAbilityLevel());
		if (DamageSpec.IsValid())
		{
			if (DamageEffectClass->IsChildOf(UAH_GE_Damage_Formula::StaticClass()))
			{
				DamageSpec.Data->SetSetByCallerMagnitude(TAG_Data_Damage, BaseDamage);
				DamageSpec.Data->SetSetByCallerMagnitude(TAG_Data_DamageMultiplier, GetDamageMultiplier(ComboStep, HitboxMultiplier, bRiposte));
				DamageSpec.Data->SetSetByCallerMagnitude(TAG_Data_AttackPowerScale, bAddAttackPower ? 1.f : 0.f);
			}
			else
			{
				// Legacy raw GE (reads Data.Damage only): hand it the final number.
				DamageSpec.Data->SetSetByCallerMagnitude(TAG_Data_Damage, DamageApplied);
			}
			DamageSpec.Data->AddDynamicAssetTag(TAG_Damage_Type_Melee);
			DamageSpec.Data->GetContext().AddHitResult(HitResult, true);
			SourceASC->ApplyGameplayEffectSpecToTarget(*DamageSpec.Data.Get(), TargetASC);
		}
	}

	if ((!bBlockedByTarget || bIgnoreBlockForPosture) && PostureDamageEffectClass && BasePostureDamage > 0.f)
	{
		const float PostureDamage = BasePostureDamage * GetStepPostureMultiplier(ComboStep) * HitboxMultiplier * (bRiposte ? RiposteDamageMultiplier : 1.f)
			* UBH_CombatIdentityComponent::GetOutgoingCombatMultiplier(GetAvatarActorFromActorInfo());

		FGameplayEffectSpecHandle PostureSpec = MakeOutgoingGameplayEffectSpec(PostureDamageEffectClass, GetAbilityLevel());
		if (PostureSpec.IsValid())
		{
			PostureSpec.Data->SetSetByCallerMagnitude(TAG_Data_PostureDamage, -PostureDamage);
			PostureSpec.Data->AddDynamicAssetTag(TAG_Damage_Type_Melee);
			SourceASC->ApplyGameplayEffectSpecToTarget(*PostureSpec.Data.Get(), TargetASC);
		}
	}

	// Riposte is consumed by the first hit of the counterattack.
	if (bRiposte)
	{
		SourceASC->RemoveActiveGameplayEffectBySourceEffect(UAH_GE_RiposteWindow::StaticClass(), SourceASC);
	}

	// Momentum (e.g. dual-sword Flurry): applied to the attacker on every successful hit.
	if (OnHitSelfEffectClass)
	{
		FGameplayEffectSpecHandle SelfSpec = MakeOutgoingGameplayEffectSpec(OnHitSelfEffectClass, GetAbilityLevel());
		if (SelfSpec.IsValid())
		{
			SourceASC->ApplyGameplayEffectSpecToSelf(*SelfSpec.Data.Get());
		}
	}

	// Tiered pushback (server only; see BH_CombatFeel.h). Not parried (returned above). The tier is the damage tier as if unblocked;
	// ApplyHitPushback halves the distance for a blocked hit and skips dead / posture-broken victims.
	if (DamageApplied > 0.f)
	{
		if (AActor* PushAttacker = GetAvatarActorFromActorInfo())
		{
			const EBH_ImpactTier PushTier = UBH_CombatFeelLibrary::TierForHit(DamageApplied, GetStepPostureMultiplier(ComboStep) * HitboxMultiplier, false, false);
			UBH_CombatFeelLibrary::ApplyHitPushback(PushAttacker, Target, PushTier, bBlockedByTarget);
		}
	}

	// Cosmetic cue (replicated to every machine): hit-stop + camera shake. Target of the cue is the attacker;
	// the victim travels in SourceObject so the cue knows both actors.
	if (AActor* AttackerActor = GetAvatarActorFromActorInfo())
	{
		FGameplayCueParameters CueParams;
		CueParams.Instigator = AttackerActor;
		CueParams.EffectCauser = AttackerActor;
		CueParams.SourceObject = Target;
		CueParams.RawMagnitude = DamageApplied;
		// Step posture multiplier x hitbox multiplier: lets the hit cue recognise finishers (UBH_CombatFeelLibrary::TierForHit).
		CueParams.NormalizedMagnitude = GetStepPostureMultiplier(ComboStep) * HitboxMultiplier;
		// Impact point of the blade sweep; left zero when unknown so the cue can fall back to chest height on the victim.
		CueParams.Location = HitResult.ImpactPoint.IsZero() ? FVector::ZeroVector : FVector(HitResult.ImpactPoint);
		CueParams.Normal = (Target->GetActorLocation() - AttackerActor->GetActorLocation()).GetSafeNormal();
		CueParams.TargetAttachComponent = Target->GetRootComponent();
		if (bBlockedByTarget)
		{
			// Lets the cue pick the blocked (metal on metal) FX instead of the flesh FX.
			CueParams.AggregatedSourceTags.AddTag(TAG_Combat_HitResult_Blocked);
		}
		SourceASC->ExecuteGameplayCue(HitCueTag.IsValid() ? HitCueTag : FGameplayTag(TAG_GameplayCue_Combat_Hit), CueParams);
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

	if (bInRecoil)
	{
		return;
	}

	K2_OnAttackParried(const_cast<AActor*>(Payload.Instigator.Get()));

	if (RecoilMontage)
	{
		PlayRecoil();
	}
	else if (bEndComboWhenParried)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
	}
}

void UAH_GA_MeleeAttack_Base::PlayRecoil()
{
	bInRecoil = true;
	bInputBuffered = false;
	bComboWindowOpen = false;

	// Detach from the attack montage task first so its interruption (caused by
	// the recoil replacing it) doesn't end the ability.
	DetachMontageTask();

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->SetLooseGameplayTagCount(TAG_State_Combat_ComboWindow, 0);
	}

	MontageTask = StartMontageTask(RecoilMontage, GetEffectivePlayRate(RecoilPlayRate));
}

UAbilityTask_PlayMontageAndWait* UAH_GA_MeleeAttack_Base::StartMontageTask(UAnimMontage* Montage, float Rate, FName StartSection)
{
	UAbilityTask_PlayMontageAndWait* Task = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this, NAME_None, Montage, Rate, StartSection);
	Task->OnCompleted.AddDynamic(this, &UAH_GA_MeleeAttack_Base::OnMontageCompleted);
	Task->OnBlendOut.AddDynamic(this, &UAH_GA_MeleeAttack_Base::OnMontageBlendOut);
	Task->OnInterrupted.AddDynamic(this, &UAH_GA_MeleeAttack_Base::OnMontageInterrupted);
	Task->OnCancelled.AddDynamic(this, &UAH_GA_MeleeAttack_Base::OnMontageCancelled);
	Task->ReadyForActivation();
	return Task;
}

void UAH_GA_MeleeAttack_Base::DetachMontageTask()
{
	if (MontageTask)
	{
		MontageTask->OnCompleted.RemoveAll(this);
		MontageTask->OnBlendOut.RemoveAll(this);
		MontageTask->OnInterrupted.RemoveAll(this);
		MontageTask->OnCancelled.RemoveAll(this);
		MontageTask->EndTask();
		MontageTask = nullptr;
	}
}

void UAH_GA_MeleeAttack_Base::CrossfadeToSection(FName SectionName)
{
	// The old task must not report the handoff (its instance is interrupted by the new play) as the end of the ability.
	DetachMontageTask();

	// A fresh Montage_Play stops the running instance of this montage's slot group and blends the new one in.
	// UAbilityTask_PlayMontageAndWait has no blend parameters, so the asset's BlendIn (used for both the new
	// instance's blend-in and the interrupted instance's blend-out) is overridden for the duration of the play call
	// only (game thread, synchronous: ReadyForActivation plays the montage immediately) and restored right after.
	const float AssetBlendIn = AttackMontage->BlendIn.GetBlendTime();
	AttackMontage->BlendIn.SetBlendTime(ComboStepBlendTime);

	MontageTask = StartMontageTask(AttackMontage, GetEffectivePlayRate(MontagePlayRate), SectionName);

	AttackMontage->BlendIn.SetBlendTime(AssetBlendIn);

	// A predicted client's montage play is local-only: tell the server which step is now playing (and how fast)
	// through the ASC's own jump / play-rate RPCs. Locally these are no-ops (already on that section, same rate).
	if (!K2_HasAuthority())
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			if (ASC->GetCurrentMontage() == AttackMontage)
			{
				ASC->CurrentMontageJumpToSection(SectionName);
				ASC->CurrentMontageSetPlayRate(GetEffectivePlayRate(MontagePlayRate));
			}
		}
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
