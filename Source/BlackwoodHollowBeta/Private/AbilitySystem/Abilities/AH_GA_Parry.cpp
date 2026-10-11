// Blackwood Hollow - Parry ability (implementation)

#include "AbilitySystem/Abilities/AH_GA_Parry.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "Combat/BH_StanceComponent.h"
#include "AbilitySystem/Effects/AH_GE_CombatEffects.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "GameplayEffectTypes.h"
#include "GameplayEffect.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Animation/AnimMontage.h"
#include "Engine/World.h"
#include "TimerManager.h"

UAH_GA_Parry::UAH_GA_Parry()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

	FGameplayTagContainer DefaultAssetTags;
	DefaultAssetTags.AddTag(TAG_Ability_Combat_Parry);
	SetAssetTags(DefaultAssetTags);

	ActivationBlockedTags.AddTag(TAG_State_Combat_Staggered);
	ActivationBlockedTags.AddTag(TAG_State_Combat_PostureBroken);
	ActivationBlockedTags.AddTag(TAG_State_Combat_Dead);
	ActivationBlockedTags.AddTag(TAG_State_Action_Consuming); // Phase 11D: cannot start while drinking / placing a consumable

	// Parry interrupts your own swing, and you can't swing mid-parry.
	CancelAbilitiesWithTag.AddTag(TAG_Ability_Combat_MeleeAttack);
	BlockAbilitiesWithTag.AddTag(TAG_Ability_Combat_MeleeAttack);

	PostureDamageEffectClass = UAH_GE_PostureDamage::StaticClass();
	RiposteWindowEffectClass = UAH_GE_RiposteWindow::StaticClass();
}

void UAH_GA_Parry::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
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

	bParryWindowActive = false;

	// Listen for incoming melee hits for the whole ability; OnIncomingHit only
	// acts while the parry frames are active.
	UAbilityTask_WaitGameplayEvent* HitTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this, TAG_Event_Combat_Hit, nullptr, false, true);
	HitTask->EventReceived.AddDynamic(this, &UAH_GA_Parry::OnIncomingHit);
	HitTask->ReadyForActivation();

	UAnimMontage* ResolvedMontage = ResolveParryMontage();
	if (ResolvedMontage)
	{
		MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this, NAME_None, ResolvedMontage, MontagePlayRate);
		MontageTask->OnCompleted.AddDynamic(this, &UAH_GA_Parry::OnMontageFinished);
		MontageTask->OnBlendOut.AddDynamic(this, &UAH_GA_Parry::OnMontageFinished);
		MontageTask->OnInterrupted.AddDynamic(this, &UAH_GA_Parry::OnMontageInterrupted);
		MontageTask->OnCancelled.AddDynamic(this, &UAH_GA_Parry::OnMontageInterrupted);
		MontageTask->ReadyForActivation();
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (ParryWindowStartDelay > 0.f)
	{
		World->GetTimerManager().SetTimer(WindowOpenTimerHandle, this, &UAH_GA_Parry::OpenParryWindow, ParryWindowStartDelay, false);
	}
	else
	{
		OpenParryWindow();
	}

	if (!ResolvedMontage)
	{
		const float TotalDuration = FMath::Max(ParryWindowStartDelay + ParryWindowDuration, RecoveryDuration);
		World->GetTimerManager().SetTimer(RecoveryTimerHandle, this, &UAH_GA_Parry::FinishParry, TotalDuration, false);
	}
}

UAnimMontage* UAH_GA_Parry::ResolveParryMontage() const
{
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	const FGameplayTag StanceTag = UBH_StanceComponent::FindStanceComponent(Avatar) ? UBH_StanceComponent::GetStanceTagOf(Avatar) : FGameplayTag();

	UAnimMontage* Resolved = ParryMontage;
	if (StanceTag.IsValid())
	{
		if (const TObjectPtr<UAnimMontage>* Found = StanceParryMontagesByTag.Find(StanceTag))
		{
			if (*Found)
			{
				Resolved = *Found;
			}
		}
	}

	UE_LOG(LogBHCombat, Verbose, TEXT("Parry: %s stance=%s montage=%s"), *GetNameSafe(Avatar), *StanceTag.ToString(), *GetNameSafe(Resolved));
	return Resolved;
}

void UAH_GA_Parry::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (UWorld* World = GetWorld())
	{
		FTimerManager& TimerManager = World->GetTimerManager();
		TimerManager.ClearTimer(WindowOpenTimerHandle);
		TimerManager.ClearTimer(WindowCloseTimerHandle);
		TimerManager.ClearTimer(RecoveryTimerHandle);
	}

	CloseParryWindow();
	MontageTask = nullptr;

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UAH_GA_Parry::OpenParryWindow()
{
	if (bParryWindowActive)
	{
		return;
	}

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->AddLooseGameplayTag(TAG_State_Combat_Parrying);
		bParryWindowActive = true;
		K2_OnParryWindowOpened();
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(WindowCloseTimerHandle, this, &UAH_GA_Parry::CloseParryWindow, ParryWindowDuration, false);
	}
}

void UAH_GA_Parry::CloseParryWindow()
{
	if (!bParryWindowActive)
	{
		return;
	}

	bParryWindowActive = false;
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->RemoveLooseGameplayTag(TAG_State_Combat_Parrying);
	}
	K2_OnParryWindowClosed();
}

void UAH_GA_Parry::FinishParry()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UAH_GA_Parry::OnMontageFinished()
{
	FinishParry();
}

void UAH_GA_Parry::OnMontageInterrupted()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}

void UAH_GA_Parry::CalculateParryPostureDamage_Implementation(AActor* Attacker, float HitMultiplier,
	float& OutAttackerPostureDamage, float& OutSelfPostureCost) const
{
	const float Scale = bScaleWithHitMultiplier ? FMath::Max(HitMultiplier, 0.f) : 1.f;

	float AttackPower = 0.f;
	if (const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		AttackPower = ASC->GetNumericAttribute(UAH_AttributeSet::GetAttackPowerAttribute());
	}

	OutAttackerPostureDamage = AttackerPostureDamage * (1.f + AttackPower * AttackPowerPostureScale) * Scale;
	OutSelfPostureCost = DefenderPostureCost * Scale;
}

void UAH_GA_Parry::ApplyPostureDelta(UAbilitySystemComponent* TargetASC, float PostureDamage) const
{
	if (!TargetASC || !PostureDamageEffectClass || PostureDamage <= 0.f)
	{
		return;
	}

	UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo();
	if (!SourceASC)
	{
		return;
	}

	FGameplayEffectSpecHandle Spec = MakeOutgoingGameplayEffectSpec(PostureDamageEffectClass, GetAbilityLevel());
	if (Spec.IsValid())
	{
		Spec.Data->SetSetByCallerMagnitude(TAG_Data_PostureDamage, -PostureDamage);
		SourceASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), TargetASC);
	}
}

void UAH_GA_Parry::OnIncomingHit(FGameplayEventData Payload)
{
	if (!bParryWindowActive)
	{
		return;
	}

	AActor* Attacker = const_cast<AActor*>(Payload.Instigator.Get());
	AActor* Self = GetAvatarActorFromActorInfo();
	if (!Attacker || Attacker == Self)
	{
		return;
	}

	float PostureToAttacker = 0.f;
	float PostureToSelf = 0.f;
	CalculateParryPostureDamage(Attacker, Payload.EventMagnitude, PostureToAttacker, PostureToSelf);

	// Attribute changes are authoritative; the damage itself is negated by
	// UAH_AttributeSet while State.Combat.Parrying is on us.
	if (K2_HasAuthority())
	{
		ApplyPostureDelta(UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Attacker), PostureToAttacker);
		ApplyPostureDelta(GetAbilitySystemComponentFromActorInfo(), PostureToSelf);

		if (UAbilitySystemComponent* SelfASC = GetAbilitySystemComponentFromActorInfo())
		{
			// Perfect parry: open the riposte window on the parrier.
			if (RiposteWindowEffectClass)
			{
				FGameplayEffectSpecHandle RiposteSpec = MakeOutgoingGameplayEffectSpec(RiposteWindowEffectClass, GetAbilityLevel());
				if (RiposteSpec.IsValid())
				{
					SelfASC->ApplyGameplayEffectSpecToSelf(*RiposteSpec.Data.Get());
				}
			}

			// Cosmetic cue (replicated): target = parrier, the attacker travels in SourceObject / Instigator.
			const FHitResult ParryHit = UAbilitySystemBlueprintLibrary::GetHitResultFromTargetData(Payload.TargetData, 0);
			FGameplayCueParameters CueParams;
			CueParams.Instigator = Attacker;
			CueParams.EffectCauser = Attacker;
			CueParams.SourceObject = Attacker;
			CueParams.RawMagnitude = PostureToAttacker;
			CueParams.Location = !ParryHit.ImpactPoint.IsZero() ? FVector(ParryHit.ImpactPoint) : Self->GetActorLocation();
			CueParams.Normal = (Attacker->GetActorLocation() - Self->GetActorLocation()).GetSafeNormal();
			CueParams.TargetAttachComponent = Self->GetRootComponent();
			SelfASC->ExecuteGameplayCue(TAG_GameplayCue_Combat_ParrySuccess, CueParams);
		}
	}

	FGameplayEventData SuccessPayload;
	SuccessPayload.EventTag = TAG_Event_Combat_Parry_Success;
	SuccessPayload.Instigator = Self;       // parrier
	SuccessPayload.Target = Attacker;       // attacker
	SuccessPayload.EventMagnitude = PostureToAttacker;
	SuccessPayload.TargetData = Payload.TargetData;

	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Attacker, TAG_Event_Combat_Parry_Success, SuccessPayload);
	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Self, TAG_Event_Combat_Parry_Success, SuccessPayload);

	K2_OnParrySuccess(Attacker, PostureToAttacker, PostureToSelf);

	if (bEndOnSuccessfulParry)
	{
		// Deferred a tick: UANS_MeleeHitbox sends Event.Combat.HitDealt to the attacker right after
		// this event, and State.Combat.Parrying must still be on us when that damage is evaluated.
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimerForNextTick(this, &UAH_GA_Parry::FinishParry);
		}
	}
}
