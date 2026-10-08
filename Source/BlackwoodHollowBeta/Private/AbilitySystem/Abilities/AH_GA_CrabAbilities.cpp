// Blackwood Hollow - the crab's three abilities (implementation)

#include "AbilitySystem/Abilities/AH_GA_CrabAbilities.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "Components/BPC_HeartFragment.h"
#include "Characters/BH_EnemyCrab.h"
#include "Components/BH_TelegraphComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"

// ============================================================================
// Jab
// ============================================================================

UAH_GA_CrabJab::UAH_GA_CrabJab()
{
	FGameplayTagContainer DefaultAssetTags;
	DefaultAssetTags.AddTag(TAG_Ability_Combat_MeleeAttack);
	SetAssetTags(DefaultAssetTags);

	ActivationOwnedTags.AddTag(TAG_State_Combat_Attacking);
	ActivationOwnedTags.AddTag(TAG_State_Combat_MovementLocked);

	// Fast and light on health, heavy on posture.
	BaseDamage = 4.f;
	BasePostureDamage = 22.f;
	RecoveryTime = 0.35f;
}

void UAH_GA_CrabJab::BeginAttack()
{
	SetPhase(EBH_CrabActionPhase::JabWindup);
	ScheduleStep(this, &UAH_GA_CrabJab::OnWindupFinished, WindupTime);
}

void UAH_GA_CrabJab::OnWindupFinished()
{
	// Last look at the target, then commit to the lunge direction.
	if (const AActor* Target = ResolveTarget())
	{
		FaceLocationNow(Target->GetActorLocation());
	}

	SetPhase(EBH_CrabActionPhase::JabStrike);
	if (const AActor* Avatar = GetAvatarActorFromActorInfo())
	{
		StartLunge(Avatar->GetActorForwardVector(), LungeSpeed * LungeDuration, LungeDuration);
	}
	StartHitScan(HitWindow, 0.03f);
}

void UAH_GA_CrabJab::HitScanTick()
{
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Avatar)
	{
		return;
	}

	const FVector Center = Avatar->GetActorLocation() + Avatar->GetActorForwardVector() * HitReach;
	TArray<AActor*> Candidates;
	CollectPawnsInSphere(Center, HitRadius, Candidates);
	for (AActor* Candidate : Candidates)
	{
		ResolveHitOnActor(Candidate, Center);
		if (WasParried())
		{
			break;
		}
	}
}

void UAH_GA_CrabJab::OnHitScanFinished()
{
	FinishAttack();
}

// ============================================================================
// Pinch
// ============================================================================

UAH_GA_CrabPinch::UAH_GA_CrabPinch()
{
	FGameplayTagContainer DefaultAssetTags;
	DefaultAssetTags.AddTag(TAG_Ability_Combat_MeleeAttack);
	SetAssetTags(DefaultAssetTags);

	ActivationOwnedTags.AddTag(TAG_State_Combat_Attacking);
	ActivationOwnedTags.AddTag(TAG_State_Combat_MovementLocked);

	BaseDamage = 8.f;
	BasePostureDamage = 12.f;
	RecoveryTime = 0.5f;
}

void UAH_GA_CrabPinch::BeginAttack()
{
	ABH_EnemyCrab* Crab = GetCrab();
	const AActor* Target = ResolveTarget();
	if (!Crab || !Target)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	// The warning marks where the target stands NOW; moving out of it is the counter-play.
	TargetPoint = GetGroundPoint(Target);
	if (UBH_TelegraphComponent* Telegraph = Crab->GetTelegraph())
	{
		Telegraph->StartTelegraph(TargetPoint, TelegraphRadius, TelegraphDuration);
	}

	SetPhase(EBH_CrabActionPhase::PinchWindup);
	ScheduleStep(this, &UAH_GA_CrabPinch::OnTelegraphFinished, TelegraphDuration);
}

void UAH_GA_CrabPinch::OnTelegraphFinished()
{
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Avatar)
	{
		return;
	}

	SetPhase(EBH_CrabActionPhase::PinchSnap);
	FaceLocationNow(TargetPoint);

	const FVector ToPoint = TargetPoint - Avatar->GetActorLocation();
	const float FlatDistance = static_cast<float>(ToPoint.Size2D());
	const float LungeDistance = FMath::Clamp(FlatDistance - StopShortDistance, 0.f, LungeMaxDistance);
	if (LungeDistance > 1.f)
	{
		StartLunge(ToPoint, LungeDistance, LungeDuration);
	}
	ScheduleStep(this, &UAH_GA_CrabPinch::OnSnapHit, SnapHitDelay);
}

void UAH_GA_CrabPinch::OnSnapHit()
{
	TArray<AActor*> Candidates;
	CollectPawnsInSphere(TargetPoint, TelegraphRadius, Candidates);
	for (AActor* Candidate : Candidates)
	{
		ResolveHitOnActor(Candidate, Candidate->GetActorLocation());
		if (WasParried())
		{
			break;
		}
	}
	FinishAttack();
}

void UAH_GA_CrabPinch::OnVictimResolved(AActor* Victim, const FBH_CrabHitOutcome& Outcome)
{
	if (!Outcome.bConnected || Outcome.bParried || (Outcome.bBlocked && !bApplyBlightWhenBlocked))
	{
		return;
	}

	// Phase 10B: build-up on the victim's Blight meter (server only; the meter mitigates and saturates itself).
	if (!Victim || !Victim->HasAuthority())
	{
		return;
	}
	if (UBPC_HeartFragment* Heart = Victim->FindComponentByClass<UBPC_HeartFragment>())
	{
		Heart->AddBlightBuildup(BlightBuildupPerHit, GetAvatarActorFromActorInfo());
	}
}

// ============================================================================
// Sidestep
// ============================================================================

UAH_GA_CrabSidestep::UAH_GA_CrabSidestep()
{
	FGameplayTagContainer DefaultAssetTags;
	DefaultAssetTags.AddTag(TAG_Ability_Combat_Dodge);
	SetAssetTags(DefaultAssetTags);

	ActivationOwnedTags.AddTag(TAG_State_Combat_Dodging);

	bRequireAttackToken = false; // a repositioning move, not an attack
	bFaceTargetOnStart = false;  // keeps facing the target through the controller focus instead of snapping
	BaseDamage = 0.f;
	BasePostureDamage = 0.f;
	RecoveryTime = 0.1f;
}

float UAH_GA_CrabSidestep::ClearDistanceAlong(const FVector& Direction) const
{
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	UWorld* World = GetWorld();
	if (!Avatar || !World)
	{
		return 0.f;
	}

	float Radius = 40.f;
	if (const ACharacter* Character = Cast<ACharacter>(Avatar))
	{
		if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
		{
			Radius = Capsule->GetScaledCapsuleRadius();
		}
	}

	const FVector Start = Avatar->GetActorLocation();
	const FVector End = Start + Direction * (Distance + Radius);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BH_CrabSidestep), false, Avatar);
	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		return FMath::Max(static_cast<float>(Hit.Distance) - Radius, 0.f);
	}
	return Distance;
}

void UAH_GA_CrabSidestep::BeginAttack()
{
	ABH_EnemyCrab* Crab = GetCrab();
	if (!Crab)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	// Perpendicular to the line to the target (or the crab's own right when it has none), random side first.
	FVector Forward = Crab->GetActorForwardVector();
	if (const AActor* Target = ResolveTarget())
	{
		const FVector ToTarget = (Target->GetActorLocation() - Crab->GetActorLocation()).GetSafeNormal2D();
		if (!ToTarget.IsNearlyZero())
		{
			Forward = ToTarget;
		}
	}
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward).GetSafeNormal();
	const float PreferredSide = FMath::RandBool() ? 1.f : -1.f;

	FVector Chosen = Right * PreferredSide;
	float Clear = ClearDistanceAlong(Chosen);
	if (Clear < MinClearDistance)
	{
		const FVector Other = -Chosen;
		const float OtherClear = ClearDistanceAlong(Other);
		if (OtherClear >= MinClearDistance)
		{
			Chosen = Other;
			Clear = OtherClear;
		}
	}
	if (Clear < MinClearDistance)
	{
		// Boxed in on both sides: no dash. Cancelled so the cooldown / phase are not consumed.
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}

	Crab->MarkSidestepUsed();
	SetPhase(EBH_CrabActionPhase::Sidestep);
	StartLunge(Chosen, FMath::Min(Distance, Clear), Duration);

	if (bGrantIFrames)
	{
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			ASC->AddLooseGameplayTag(TAG_State_Combat_Invulnerable);
			bIFramesActive = true;
			if (UWorld* World = GetWorld())
			{
				World->GetTimerManager().SetTimer(IFrameTimer, this, &UAH_GA_CrabSidestep::OnIFramesFinished, FMath::Max(IFrameDuration, 0.01f), false);
			}
		}
	}

	ScheduleStep(this, &UAH_GA_CrabSidestep::OnDashFinished, Duration);
}

void UAH_GA_CrabSidestep::OnDashFinished()
{
	RemoveIFrames();
	FinishAttack();
}

void UAH_GA_CrabSidestep::OnIFramesFinished()
{
	RemoveIFrames();
}

void UAH_GA_CrabSidestep::RemoveIFrames()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(IFrameTimer);
	}
	if (bIFramesActive)
	{
		bIFramesActive = false;
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			ASC->RemoveLooseGameplayTag(TAG_State_Combat_Invulnerable);
		}
	}
}

void UAH_GA_CrabSidestep::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	RemoveIFrames();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
