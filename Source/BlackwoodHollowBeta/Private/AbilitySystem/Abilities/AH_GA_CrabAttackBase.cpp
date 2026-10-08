// Blackwood Hollow - crab ability base (implementation)

#include "AbilitySystem/Abilities/AH_GA_CrabAttackBase.h"
#include "AI/BH_AttackTokenSubsystem.h"
#include "AI/BH_CrabAIController.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystem/Abilities/AH_GA_Block.h"
#include "AbilitySystem/BH_CombatFunctionLibrary.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystem/Effects/AH_GE_CombatEffects.h"
#include "Characters/BH_EnemyCrab.h"
#include "Combat/BH_CombatFeel.h"
#include "Combat/BH_CombatIdentityComponent.h"
#include "Components/BH_TelegraphComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/RootMotionSource.h"
#include "Components/CapsuleComponent.h"
#include "Engine/OverlapResult.h"

UAH_GA_CrabAttackBase::UAH_GA_CrabAttackBase()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;

	ActivationBlockedTags.AddTag(TAG_State_Combat_Staggered);
	ActivationBlockedTags.AddTag(TAG_State_Combat_PostureBroken);
	ActivationBlockedTags.AddTag(TAG_State_Combat_Dead);
	ActivationBlockedTags.AddTag(TAG_State_Combat_Attacking);
	ActivationBlockedTags.AddTag(TAG_State_Combat_Dodging);

	DamageEffectClass = UAH_GE_Damage_Formula::StaticClass();
	PostureDamageEffectClass = UAH_GE_PostureDamage::StaticClass();
	HitCueTag = TAG_GameplayCue_Combat_Hit;
}

ABH_EnemyCrab* UAH_GA_CrabAttackBase::GetCrab() const
{
	return Cast<ABH_EnemyCrab>(GetAvatarActorFromActorInfo());
}

bool UAH_GA_CrabAttackBase::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	if (bRequireAttackToken && UBH_AttackTokenSubsystem::bEnableAttackTokens && ActorInfo && ActorInfo->AvatarActor.IsValid())
	{
		const APawn* Pawn = Cast<APawn>(ActorInfo->AvatarActor.Get());
		const ABH_CrabAIController* CrabController = Pawn ? Cast<ABH_CrabAIController>(Pawn->GetController()) : nullptr;
		if (CrabController && !CrabController->HasAttackToken())
		{
			return false;
		}
	}
	return true;
}

void UAH_GA_CrabAttackBase::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	HitActors.Reset();
	bParried = false;
	bFinishing = false;

	if (bFaceTargetOnStart)
	{
		if (const AActor* Target = ResolveTarget())
		{
			FaceLocationNow(Target->GetActorLocation());
		}
	}

	BeginAttack();
}

void UAH_GA_CrabAttackBase::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	CancelStep();
	StopHitScan();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RecoveryTimer);
	}
	StopLunge();

	if (ABH_EnemyCrab* Crab = GetCrab())
	{
		if (bWasCancelled)
		{
			if (UBH_TelegraphComponent* Telegraph = Crab->GetTelegraph())
			{
				if (Telegraph->IsTelegraphActive())
				{
					Telegraph->CancelTelegraph();
				}
			}
		}
		Crab->SetAbilityPhase(EBH_CrabActionPhase::Idle);
	}

	HitActors.Reset();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

// ============================================================================
// Helpers
// ============================================================================

AActor* UAH_GA_CrabAttackBase::ResolveTarget() const
{
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	if (const APawn* Pawn = Cast<APawn>(Avatar))
	{
		if (const ABH_CrabAIController* CrabController = Cast<ABH_CrabAIController>(Pawn->GetController()))
		{
			if (AActor* ControllerTarget = CrabController->GetCrabTarget())
			{
				return ControllerTarget;
			}
		}
	}

	UWorld* World = GetWorld();
	if (!Avatar || !World)
	{
		return nullptr;
	}
	AActor* Best = nullptr;
	float BestDistSq = FMath::Square(2500.f);
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		APawn* Candidate = *It;
		if (Candidate == Avatar || UBH_CombatFunctionLibrary::AreCombatAllies(Avatar, Candidate))
		{
			continue;
		}
		const UAbilitySystemComponent* CandidateASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Candidate);
		if (!CandidateASC || CandidateASC->HasMatchingGameplayTag(TAG_State_Combat_Dead))
		{
			continue;
		}
		const float DistSq = FVector::DistSquared(Avatar->GetActorLocation(), Candidate->GetActorLocation());
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = Candidate;
		}
	}
	return Best;
}

void UAH_GA_CrabAttackBase::SetPhase(EBH_CrabActionPhase Phase) const
{
	if (ABH_EnemyCrab* Crab = GetCrab())
	{
		Crab->SetAbilityPhase(Phase);
	}
}

void UAH_GA_CrabAttackBase::FaceLocationNow(const FVector& Location) const
{
	AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Avatar)
	{
		return;
	}
	const FVector ToLocation = (Location - Avatar->GetActorLocation()).GetSafeNormal2D();
	if (ToLocation.IsNearlyZero())
	{
		return;
	}
	Avatar->SetActorRotation(FRotator(0.f, ToLocation.Rotation().Yaw, 0.f));
}

FVector UAH_GA_CrabAttackBase::GetGroundPoint(const AActor* Actor)
{
	if (!Actor)
	{
		return FVector::ZeroVector;
	}
	FVector Point = Actor->GetActorLocation();
	if (const ACharacter* Character = Cast<ACharacter>(Actor))
	{
		if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
		{
			Point.Z -= Capsule->GetScaledCapsuleHalfHeight();
		}
	}
	return Point;
}

void UAH_GA_CrabAttackBase::CollectPawnsInSphere(const FVector& Center, float Radius, TArray<AActor*>& OutActors) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BH_CrabHitScan), false, GetAvatarActorFromActorInfo());
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, Center, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn),
		FCollisionShape::MakeSphere(Radius), Params);
	for (const FOverlapResult& Overlap : Overlaps)
	{
		if (AActor* Actor = Overlap.GetActor())
		{
			OutActors.AddUnique(Actor);
		}
	}
}

float UAH_GA_CrabAttackBase::GetDamageMultiplier() const
{
	return AttackDamageMultiplier * UBH_CombatIdentityComponent::GetOutgoingCombatMultiplier(GetAvatarActorFromActorInfo());
}

float UAH_GA_CrabAttackBase::CalculateDamage(const AActor* Target) const
{
	const UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo();
	float AttackPower = 0.f;
	if (SourceASC && SourceASC->HasAttributeSetForAttribute(UAH_AttributeSet::GetAttackPowerAttribute()))
	{
		AttackPower = SourceASC->GetNumericAttribute(UAH_AttributeSet::GetAttackPowerAttribute());
	}

	float Defense = 0.f;
	if (const UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(const_cast<AActor*>(Target)))
	{
		if (TargetASC->HasAttributeSetForAttribute(UAH_AttributeSet::GetDefenseAttribute()))
		{
			Defense = TargetASC->GetNumericAttribute(UAH_AttributeSet::GetDefenseAttribute());
		}
	}

	return UBH_CombatFunctionLibrary::ComputeDamage(BaseDamage, AttackPower, bAddAttackPower ? 1.f : 0.f, GetDamageMultiplier(), Defense);
}

FBH_CrabHitOutcome UAH_GA_CrabAttackBase::ResolveHitOnActor(AActor* Victim, const FVector& ImpactPoint, bool bAllowRepeat)
{
	FBH_CrabHitOutcome Outcome;
	AActor* Avatar = GetAvatarActorFromActorInfo();
	UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo();
	if (!Victim || !Avatar || Victim == Avatar || !SourceASC || !IsValid(Victim))
	{
		return Outcome;
	}
	const TWeakObjectPtr<AActor> WeakVictim(Victim);
	if (!bAllowRepeat && HitActors.Contains(WeakVictim))
	{
		return Outcome;
	}
	if (UBH_CombatFunctionLibrary::AreCombatAllies(Avatar, Victim))
	{
		return Outcome;
	}

	UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Victim);
	if (!TargetASC || TargetASC->HasMatchingGameplayTag(TAG_State_Combat_Dead) || TargetASC->HasMatchingGameplayTag(TAG_State_Combat_Invulnerable))
	{
		return Outcome;
	}
	HitActors.AddUnique(WeakVictim);

	FHitResult HitResult;
	HitResult.ImpactPoint = ImpactPoint;
	HitResult.Location = ImpactPoint;
	HitResult.ImpactNormal = (Avatar->GetActorLocation() - Victim->GetActorLocation()).GetSafeNormal();
	HitResult.Normal = HitResult.ImpactNormal;

	// 1) Victim first, so an active parry can react before damage (same order as UANS_MeleeHitbox).
	FGameplayEventData Payload;
	Payload.EventTag = TAG_Event_Combat_Hit;
	Payload.Instigator = Avatar;
	Payload.Target = Victim;
	Payload.EventMagnitude = 1.f;
	Payload.TargetData = UAbilitySystemBlueprintLibrary::AbilityTargetDataFromHitResult(HitResult);
	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Victim, TAG_Event_Combat_Hit, Payload);

	// 2) Parried: the parry ability handled posture; the attribute set rejects melee damage while State.Combat.Parrying is on.
	if (TargetASC->HasMatchingGameplayTag(TAG_State_Combat_Parrying))
	{
		Outcome.bParried = true;
		bParried = true;
		OnVictimResolved(Victim, Outcome);
		return Outcome;
	}

	// 3) Blocked? Evaluated before damage (a posture break would drop the guard).
	if (TargetASC->HasMatchingGameplayTag(TAG_State_Combat_Blocking))
	{
		if (const UAH_GA_Block* ActiveBlock = UAH_GA_Block::FindActiveBlock(TargetASC))
		{
			Outcome.bBlocked = ActiveBlock->IsAttackInBlockArc(Avatar);
		}
	}

	// 4) Damage and posture.
	float DamageApplied = 0.f;
	if (DamageEffectClass)
	{
		// DamageApplied is the real number for hit-feel (same inputs as the GE); the formula GE gets the raw inputs instead.
		DamageApplied = CalculateDamage(Victim);
		FGameplayEffectSpecHandle DamageSpec = MakeOutgoingGameplayEffectSpec(DamageEffectClass, GetAbilityLevel());
		if (DamageSpec.IsValid() && DamageSpec.Data.IsValid())
		{
			if (DamageEffectClass->IsChildOf(UAH_GE_Damage_Formula::StaticClass()))
			{
				DamageSpec.Data->SetSetByCallerMagnitude(TAG_Data_Damage, BaseDamage);
				DamageSpec.Data->SetSetByCallerMagnitude(TAG_Data_DamageMultiplier, GetDamageMultiplier());
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

	if ((!Outcome.bBlocked || bIgnoreBlockForPosture) && PostureDamageEffectClass && BasePostureDamage > 0.f)
	{
		const float PostureDamage = BasePostureDamage * UBH_CombatIdentityComponent::GetOutgoingCombatMultiplier(Avatar);
		FGameplayEffectSpecHandle PostureSpec = MakeOutgoingGameplayEffectSpec(PostureDamageEffectClass, GetAbilityLevel());
		if (PostureSpec.IsValid() && PostureSpec.Data.IsValid())
		{
			PostureSpec.Data->SetSetByCallerMagnitude(TAG_Data_PostureDamage, -PostureDamage);
			PostureSpec.Data->AddDynamicAssetTag(TAG_Damage_Type_Melee);
			SourceASC->ApplyGameplayEffectSpecToTarget(*PostureSpec.Data.Get(), TargetASC);
		}
	}

	// 5) Pushback and the cosmetic cue.
	if (DamageApplied > 0.f)
	{
		const EBH_ImpactTier Tier = UBH_CombatFeelLibrary::TierForHit(DamageApplied, 1.f, false, false);
		UBH_CombatFeelLibrary::ApplyHitPushback(Avatar, Victim, Tier, Outcome.bBlocked);
	}

	FGameplayCueParameters CueParams;
	CueParams.Instigator = Avatar;
	CueParams.EffectCauser = Avatar;
	CueParams.SourceObject = Victim;
	CueParams.RawMagnitude = DamageApplied;
	CueParams.NormalizedMagnitude = 1.f;
	CueParams.Location = ImpactPoint.IsZero() ? FVector::ZeroVector : ImpactPoint;
	CueParams.Normal = (Victim->GetActorLocation() - Avatar->GetActorLocation()).GetSafeNormal();
	CueParams.TargetAttachComponent = Victim->GetRootComponent();
	if (Outcome.bBlocked)
	{
		CueParams.AggregatedSourceTags.AddTag(TAG_Combat_HitResult_Blocked);
	}
	// Server-evaluated after the damage GE ran: clients' copy of the victim's Health may not have replicated when the cue fires.
	if (TargetASC->HasMatchingGameplayTag(TAG_State_Combat_Dead)
		|| (TargetASC->HasAttributeSetForAttribute(UAH_AttributeSet::GetHealthAttribute()) && TargetASC->GetNumericAttribute(UAH_AttributeSet::GetHealthAttribute()) <= 0.f))
	{
		CueParams.AggregatedSourceTags.AddTag(TAG_Combat_HitResult_Fatal);
	}
	SourceASC->ExecuteGameplayCue(HitCueTag.IsValid() ? HitCueTag : FGameplayTag(TAG_GameplayCue_Combat_Hit), CueParams);

	Outcome.bConnected = true;
	Outcome.DamageApplied = DamageApplied;
	OnVictimResolved(Victim, Outcome);
	return Outcome;
}

// ============================================================================
// Lunge / hit window / recovery
// ============================================================================

void UAH_GA_CrabAttackBase::StartLunge(const FVector& Direction, float Distance, float Duration)
{
	ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo());
	UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	const FVector FlatDirection = Direction.GetSafeNormal2D();
	if (!Movement || FlatDirection.IsNearlyZero() || Distance <= 0.f || Duration <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	StopLunge();
	LungeSourceName = FName(TEXT("BH_CrabLunge"));

	// Same recipe as UBH_CombatFeelLibrary::ApplyPushbackSource, but OVERRIDE so the lunge replaces AI locomotion while it runs.
	TSharedPtr<FRootMotionSource_ConstantForce> Source = MakeShared<FRootMotionSource_ConstantForce>();
	Source->InstanceName = LungeSourceName;
	Source->AccumulateMode = ERootMotionAccumulateMode::Override;
	Source->Priority = 10;
	Source->Force = FlatDirection * (Distance / Duration);
	Source->Duration = Duration;
	Source->StrengthOverTime = nullptr;
	Source->Settings.SetFlag(ERootMotionSourceSettingsFlags::IgnoreZAccumulate);
	Source->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::SetVelocity;
	Source->FinishVelocityParams.SetVelocity = FVector::ZeroVector;
	Source->FinishVelocityParams.ClampVelocity = 0.f;
	Movement->ApplyRootMotionSource(Source);
}

void UAH_GA_CrabAttackBase::StopLunge()
{
	if (LungeSourceName.IsNone())
	{
		return;
	}
	if (const ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			Movement->RemoveRootMotionSource(LungeSourceName);
		}
	}
	LungeSourceName = NAME_None;
}

void UAH_GA_CrabAttackBase::StartHitScan(float Duration, float Interval)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	StopHitScan();
	HitScanEndTime = World->GetTimeSeconds() + FMath::Max(Duration, 0.f);
	World->GetTimerManager().SetTimer(HitScanTimer, this, &UAH_GA_CrabAttackBase::HitScanTickInternal, FMath::Max(Interval, 0.01f), true, 0.f);
}

void UAH_GA_CrabAttackBase::StopHitScan()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HitScanTimer);
	}
}

void UAH_GA_CrabAttackBase::HitScanTickInternal()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (!bParried)
	{
		HitScanTick();
	}
	if (bParried || World->GetTimeSeconds() >= HitScanEndTime)
	{
		StopHitScan();
		OnHitScanFinished();
	}
}

void UAH_GA_CrabAttackBase::CancelStep()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(StepTimer);
	}
}

void UAH_GA_CrabAttackBase::FinishAttack()
{
	if (bFinishing)
	{
		return;
	}
	bFinishing = true;
	CancelStep();
	StopHitScan();
	SetPhase(EBH_CrabActionPhase::Idle);

	UWorld* World = GetWorld();
	if (World && RecoveryTime > KINDA_SMALL_NUMBER)
	{
		World->GetTimerManager().SetTimer(RecoveryTimer, this, &UAH_GA_CrabAttackBase::FinishAttackNow, RecoveryTime, false);
	}
	else
	{
		FinishAttackNow();
	}
}

void UAH_GA_CrabAttackBase::FinishAttackNow()
{
	if (IsActive())
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
	}
}
