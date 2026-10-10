// Blackwood Hollow - melee AI brain (implementation)

#include "AI/BH_AIController.h"
#include "AI/BH_AttackTokenSubsystem.h"
#include "Combat/BH_CombatIdentityComponent.h"
#include "Combat/BH_CombatTeam.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystem/BH_CombatFunctionLibrary.h"
#include "AbilitySystem/Abilities/AH_GA_MeleeAttack_Base.h"
#include "Characters/BH_CharacterBase.h"
#include "Characters/BH_EnemyBase.h"
#include "Characters/BH_CharacterTypes.h"
#include "Combat/BH_StanceComponent.h"
#include "GenericTeamAgentInterface.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Abilities/GameplayAbility.h"
#include "Navigation/PathFollowingComponent.h"
#include "GameFramework/Pawn.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"

ABH_AIController::ABH_AIController()
{
	PrimaryActorTick.bCanEverTick = true;
	SetGenericTeamId(BH_CombatTeam::ToGenericTeamId(EBH_CombatTeam::Enemies));
}

// ============================================================================
// Possession
// ============================================================================

void ABH_AIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	if (const UBH_CombatIdentityComponent* Identity = UBH_CombatIdentityComponent::Find(InPawn))
	{
		SetGenericTeamId(BH_CombatTeam::ToGenericTeamId(Identity->CombatTeam));
	}
	else if (const IGenericTeamAgentInterface* PawnAgent = Cast<IGenericTeamAgentInterface>(InPawn))
	{
		SetGenericTeamId(PawnAgent->GetGenericTeamId());
	}

	// GASP only turns a character toward the controller's rotation while WantsToStrafe is set (default true).
	UBH_CombatFunctionLibrary::SetCharacterWantsToStrafe(InPawn, true);

	State = EBH_AIState::Idle;
	ScanTimer = 0.f;
}

void ABH_AIController::OnUnPossess()
{
	if (bBlocking)
	{
		UBH_CombatFunctionLibrary::HandleBlockInput(GetPawn(), FindGrantedAbilityClass(TAG_Ability_Combat_Block), false);
		bBlocking = false;
	}
	ReleaseAttackToken();
	SetTarget(nullptr);
	Super::OnUnPossess();
}

// ============================================================================
// Helpers
// ============================================================================

UAbilitySystemComponent* ABH_AIController::GetPawnASC() const
{
	return UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetPawn());
}

UClass* ABH_AIController::FindGrantedAbilityClass(const FGameplayTag& AbilityTag) const
{
	const UAbilitySystemComponent* ASC = GetPawnASC();
	if (!ASC)
	{
		return nullptr;
	}
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (Spec.Ability && Spec.Ability->GetAssetTags().HasTag(AbilityTag))
		{
			return Spec.Ability->GetClass();
		}
	}
	return nullptr;
}

bool ABH_AIController::IsAbilityActive(UClass* AbilityClass) const
{
	const UAbilitySystemComponent* ASC = GetPawnASC();
	if (!ASC || !AbilityClass)
	{
		return false;
	}
	const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromClass(AbilityClass);
	return Spec && Spec->IsActive();
}

float ABH_AIController::GetStamina() const
{
	const UAbilitySystemComponent* ASC = GetPawnASC();
	if (!ASC || !ASC->HasAttributeSetForAttribute(UAH_AttributeSet::GetStaminaAttribute()))
	{
		return 100000.f;
	}
	return ASC->GetNumericAttribute(UAH_AttributeSet::GetStaminaAttribute());
}

float ABH_AIController::GetDistanceToTarget() const
{
	const APawn* Me = GetPawn();
	const AActor* T = Target.Get();
	return (Me && T) ? FVector::Dist2D(Me->GetActorLocation(), T->GetActorLocation()) : TNumericLimits<float>::Max();
}

float ABH_AIController::GetAngleToTarget() const
{
	const APawn* Me = GetPawn();
	const AActor* T = Target.Get();
	if (!Me || !T)
	{
		return 180.f;
	}
	const FVector ToTarget = (T->GetActorLocation() - Me->GetActorLocation()).GetSafeNormal2D();
	const FVector Forward = Me->GetActorForwardVector().GetSafeNormal2D();
	if (ToTarget.IsNearlyZero() || Forward.IsNearlyZero())
	{
		return 0.f;
	}
	return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Forward, ToTarget), -1.f, 1.f)));
}

void ABH_AIController::FaceTarget(float DeltaSeconds, float MinAngle)
{
	APawn* Me = GetPawn();
	const AActor* T = Target.Get();
	if (!Me || !T || AssistTurnSpeed <= 0.f || GetAngleToTarget() <= MinAngle)
	{
		return;
	}
	const FVector ToTarget = (T->GetActorLocation() - Me->GetActorLocation()).GetSafeNormal2D();
	if (ToTarget.IsNearlyZero())
	{
		return;
	}
	FRotator Rot = Me->GetActorRotation();
	Rot.Yaw = FMath::FixedTurn(Rot.Yaw, ToTarget.Rotation().Yaw, AssistTurnSpeed * DeltaSeconds);
	Me->SetActorRotation(Rot);
}

int32 ABH_AIController::RollComboLength() const
{
	const float Total = ComboWeight1 + ComboWeight2 + ComboWeight3;
	if (Total <= KINDA_SMALL_NUMBER)
	{
		return 1;
	}
	const float Roll = FMath::FRandRange(0.f, Total);
	if (Roll < ComboWeight1)
	{
		return 1;
	}
	return Roll < ComboWeight1 + ComboWeight2 ? 2 : 3;
}

void ABH_AIController::SetState(EBH_AIState NewState)
{
	if (State == NewState)
	{
		return;
	}
	UE_LOG(LogBHCombat, Verbose, TEXT("%s brain: %d -> %d"), *GetNameSafe(GetPawn()), static_cast<int32>(State), static_cast<int32>(NewState));
	State = NewState;
	StateTime = 0.f;
	if (NewState != EBH_AIState::Approach)
	{
		if (ABH_CharacterBase* BHPawn = Cast<ABH_CharacterBase>(GetPawn()))
		{
			BHPawn->SetAIDesiredGait(EBH_Gait::Run); // movement is stopped / locked outside Approach
		}
	}
}

// ============================================================================
// Targeting
// ============================================================================

bool ABH_AIController::IsValidHostile(const APawn* Candidate, float MaxRange) const
{
	const APawn* Me = GetPawn();
	if (!Me || !IsValid(Candidate) || Candidate == Me)
	{
		return false;
	}
	const UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(const_cast<APawn*>(Candidate));
	if (!ASC || ASC->HasMatchingGameplayTag(TAG_State_Combat_Dead))
	{
		return false;
	}
	if (ASC->HasAttributeSetForAttribute(UAH_AttributeSet::GetHealthAttribute())
		&& ASC->GetNumericAttribute(UAH_AttributeSet::GetHealthAttribute()) <= 0.f)
	{
		return false;
	}
	if (UBH_CombatFunctionLibrary::AreCombatAllies(Me, Candidate))
	{
		return false;
	}
	return FVector::Dist(Me->GetActorLocation(), Candidate->GetActorLocation()) <= MaxRange;
}

void ABH_AIController::ScanForTarget()
{
	const APawn* Me = GetPawn();
	UWorld* World = GetWorld();
	if (!Me || !World)
	{
		return;
	}

	const APawn* Best = nullptr;
	float BestDist = TNumericLimits<float>::Max();
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		const APawn* Candidate = *It;
		if (!IsValidHostile(Candidate, AggroRange))
		{
			continue;
		}
		const float Dist = FVector::Dist(Me->GetActorLocation(), Candidate->GetActorLocation());
		if (Dist >= BestDist)
		{
			continue;
		}

		FCollisionQueryParams Params(SCENE_QUERY_STAT(BH_AIAcquireLOS), false);
		Params.AddIgnoredActor(Me);
		Params.AddIgnoredActor(Candidate);
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, Me->GetPawnViewLocation(), Candidate->GetActorLocation(), ECC_Visibility, Params))
		{
			continue;
		}
		Best = Candidate;
		BestDist = Dist;
	}

	AActor* Current = Target.Get();
	if (Best && Best != Current)
	{
		// Only switch to a clearly nearer hostile (or when there is no valid current target).
		const bool bCurrentValid = Current && IsValidHostile(Cast<APawn>(Current), LoseTargetRange);
		if (!bCurrentValid || BestDist + 150.f < GetDistanceToTarget())
		{
			SetTarget(const_cast<APawn*>(Best));
		}
	}
}

void ABH_AIController::SetTarget(AActor* NewTarget)
{
	if (Target.Get() == NewTarget)
	{
		return;
	}

	// A token is per target: give the old target's back before switching (or losing) it.
	ReleaseAttackToken();

	// Stop listening to the previous target's attack state.
	if (UAbilitySystemComponent* OldASC = WatchedTargetASC.Get())
	{
		OldASC->RegisterGameplayTagEvent(TAG_State_Combat_Attacking, EGameplayTagEventType::NewOrRemoved).Remove(TargetAttackingHandle);
	}
	TargetAttackingHandle.Reset();
	WatchedTargetASC = nullptr;

	Target = NewTarget;
	AggressionTimeLeft = AggressionDelay;

	// Tell clients who we are fighting (replicated on the identity component): the boss health bar shows on the targeted player's HUD.
	if (UBH_CombatIdentityComponent* Identity = UBH_CombatIdentityComponent::Find(GetPawn()))
	{
		Identity->SetAggroTarget(NewTarget);
	}
	else if (ABH_EnemyBase* EnemyBase = Cast<ABH_EnemyBase>(GetPawn()))
	{
		// Phase 11F (#21): an enemy without an identity component reports its aggro through ABH_EnemyBase so the party-combat state sees it.
		EnemyBase->SetAggroTarget(NewTarget);
	}

	if (NewTarget)
	{
		if (UAbilitySystemComponent* NewASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(NewTarget))
		{
			TargetAttackingHandle = NewASC->RegisterGameplayTagEvent(TAG_State_Combat_Attacking, EGameplayTagEventType::NewOrRemoved)
				.AddUObject(this, &ABH_AIController::OnTargetAttackingChanged);
			WatchedTargetASC = NewASC;
		}
		SetFocus(NewTarget);
		// Acquiring a target draws the weapon (authority; replicates through the stance component).
		if (const UBH_StanceComponent* Stance = UBH_StanceComponent::FindStanceComponent(GetPawn()))
		{
			const_cast<UBH_StanceComponent*>(Stance)->NotifyCombatActivity();
		}
		if (State == EBH_AIState::Idle)
		{
			SetState(EBH_AIState::Approach);
		}
	}
	else
	{
		StopMovement();
		ClearFocus(EAIFocusPriority::Gameplay);
		if (State != EBH_AIState::Paused)
		{
			SetState(EBH_AIState::Idle);
		}
	}
}

// ============================================================================
// Party-wipe reset (Phase 11F)
// ============================================================================

void ABH_AIController::ResetAI()
{
	EndDefend();
	ReleaseAttackToken();
	SetTarget(nullptr);
	StopMovement();
	ClearFocus(EAIFocusPriority::Gameplay);
	bAttackStarted = false;
	ComboLength = 0;
	SetState(EBH_AIState::Idle);
	ScanTimer = TargetScanInterval;
}

void ABH_AIController::SetResetHold(bool bHold)
{
	if (bHold == bResetHold)
	{
		return;
	}
	bResetHold = bHold;
	ABH_CharacterBase* BHPawn = Cast<ABH_CharacterBase>(GetPawn());
	if (bHold)
	{
		ResetAI();
		if (BHPawn)
		{
			BHPawn->SetAIDesiredGait(EBH_Gait::Walk); // the walk home is a walk
		}
	}
	else
	{
		if (BHPawn)
		{
			BHPawn->SetAIDesiredGait(EBH_Gait::Run);
		}
		SetState(EBH_AIState::Idle);
		ScanTimer = 0.f;
	}
}

// ============================================================================
// Defend (reactive)
// ============================================================================

void ABH_AIController::OnTargetAttackingChanged(const FGameplayTag Tag, int32 NewCount)
{
	if (NewCount <= 0 || !GetPawn() || !Target.IsValid())
	{
		return;
	}
	if (State == EBH_AIState::Paused || State == EBH_AIState::Attack || State == EBH_AIState::Defend || DefendCooldownLeft > 0.f)
	{
		return;
	}
	if (GetDistanceToTarget() > AttackRange + DefendRangeExtra)
	{
		return;
	}

	DefendCooldownLeft = DefendCooldown;
	const float Roll = FMath::FRand();
	if (Roll < ParryChance)
	{
		BeginDefend(true);
	}
	else if (Roll < ParryChance + BlockChance)
	{
		BeginDefend(false);
	}
	// Otherwise: take it.
}

void ABH_AIController::BeginDefend(bool bParry)
{
	StopMovement();
	if (Target.IsValid())
	{
		SetFocus(Target.Get());
	}
	bDefendIsParry = bParry;
	bParryFired = false;
	ReactionTimeLeft = FMath::FRandRange(ReactionDelayMin, FMath::Max(ReactionDelayMin, ReactionDelayMax));
	DefendTimeLeft = ReactionTimeLeft + (bParry ? 0.7f : BlockHoldTime);
	SetState(EBH_AIState::Defend);
}

void ABH_AIController::EndDefend()
{
	if (bBlocking)
	{
		UBH_CombatFunctionLibrary::HandleBlockInput(GetPawn(), FindGrantedAbilityClass(TAG_Ability_Combat_Block), false);
		bBlocking = false;
	}
	bParryFired = false;
}

void ABH_AIController::TickDefend(float DeltaSeconds)
{
	APawn* Me = GetPawn();
	FaceTarget(DeltaSeconds, 10.f);

	ReactionTimeLeft -= DeltaSeconds;
	DefendTimeLeft -= DeltaSeconds;

	if (!bParryFired && ReactionTimeLeft <= 0.f)
	{
		bParryFired = true;
		if (bDefendIsParry)
		{
			++StatParriesTried;
			UBH_CombatFunctionLibrary::HandleParryInput(Me);
		}
		else
		{
			++StatBlocksTried;
			bBlocking = UBH_CombatFunctionLibrary::HandleBlockInput(Me, FindGrantedAbilityClass(TAG_Ability_Combat_Block), true);
		}
	}

	if (DefendTimeLeft <= 0.f)
	{
		EndDefend();
		SetState(Target.IsValid() ? EBH_AIState::Approach : EBH_AIState::Idle);
	}
}

// ============================================================================
// Pause (Staggered / PostureBroken / Dead)
// ============================================================================

void ABH_AIController::EnterPaused()
{
	ReleaseAttackToken();
	StopMovement();
	ClearFocus(EAIFocusPriority::Gameplay);
	if (bBlocking)
	{
		UBH_CombatFunctionLibrary::HandleBlockInput(GetPawn(), FindGrantedAbilityClass(TAG_Ability_Combat_Block), false);
		bBlocking = false;
	}
	bAttackStarted = false;
	bParryFired = false;
	ComboLength = 0;
	SetState(EBH_AIState::Paused);
}

// ============================================================================
// Approach / Attack / Recover
// ============================================================================

void ABH_AIController::TickApproach(float DeltaSeconds)
{
	APawn* Me = GetPawn();
	AActor* T = Target.Get();
	if (!Me || !T)
	{
		SetState(EBH_AIState::Idle);
		return;
	}

	SetFocus(T);
	const float Dist = GetDistanceToTarget();

	// GASP derives the gait from IA_Move (zero for AI): hand it the desired gait through the replicated base property.
	if (ABH_CharacterBase* BHPawn = Cast<ABH_CharacterBase>(Me))
	{
		BHPawn->SetAIDesiredGait(Dist >= SprintDistance ? EBH_Gait::Sprint : (Dist <= AttackRange + WalkInDistance ? EBH_Gait::Walk : EBH_Gait::Run));
	}

	if (Dist <= AttackRange)
	{
		if (GetPathFollowingComponent() && GetPathFollowingComponent()->GetStatus() != EPathFollowingStatus::Idle)
		{
			StopMovement();
		}
		FaceTarget(DeltaSeconds, 5.f);
		AggressionTimeLeft -= DeltaSeconds;
		if (Dist < MinAttackDistance && bBackOffWhenTooClose)
		{
			// Too close for the blade arc to connect: ease back (still facing the target via focus) before swinging.
			FVector Away = (Me->GetActorLocation() - T->GetActorLocation()).GetSafeNormal2D();
			if (Away.IsNearlyZero())
			{
				Away = -Me->GetActorForwardVector().GetSafeNormal2D(); // standing exactly on the target: just step back
			}
			Me->AddMovementInput(Away, 1.f);
			return;
		}
		if (AggressionTimeLeft <= 0.f && GetAngleToTarget() <= AttackFacingAngle && GetStamina() >= MinStaminaToSwing && !IsAbilityActive(FindGrantedAbilityClass(TAG_Ability_Combat_Dodge)))
		{
			// Swarm mutex: only the token holder swings; everyone else menaces from a distance.
			if (!AcquireAttackToken())
			{
				EnterCircle();
				return;
			}
			BeginAttack();
		}
		return;
	}

	// Close the gap (the aggression delay restarts every time the target leaves attack range).
	AggressionTimeLeft = AggressionDelay;
	FaceTarget(DeltaSeconds, 45.f);
	MoveReissueTimer -= DeltaSeconds;
	if (MoveReissueTimer <= 0.f)
	{
		MoveReissueTimer = 0.6f; // re-target periodically so a moving goal is tracked
		// The path-following reach test adds the agent's radius to the acceptance radius: take it out again so the
		// move ends inside AttackRange (centre to centre).
		// Never stop inside the attack band's lower edge (MinAttackDistance + margin), otherwise drift pushes us under
		// MinAttackDistance and the back-off / re-approach loop stalls. Clamped so it can't exceed AttackRange.
		const float DesiredStop = FMath::Min(FMath::Max(AttackRange - AcceptanceSlack, MinAttackDistance + MinAttackBandMargin), AttackRange);
		const float AcceptRadius = FMath::Max(0.f, DesiredStop - Me->GetSimpleCollisionRadius());
		MoveToActor(T, AcceptRadius, /*bStopOnOverlap*/ false, /*bUsePathfinding*/ true, /*bCanStrafe*/ true);
	}
	if (bDirectMoveFallback && GetMoveStatus() != EPathFollowingStatus::Moving)
	{
		// No path (no navmesh) or the path ended a little short: walk straight at the target.
		Me->AddMovementInput((T->GetActorLocation() - Me->GetActorLocation()).GetSafeNormal2D(), 1.f);
	}
}

void ABH_AIController::BeginAttack()
{
	APawn* Me = GetPawn();
	UClass* MeleeClass = FindGrantedAbilityClass(TAG_Ability_Combat_MeleeAttack);
	if (!Me || !MeleeClass)
	{
		ReleaseAttackToken(); // nothing to swing with: do not sit on the target's token
		return;
	}

	StopMovement();
	// The player's swing snaps toward the lock-on target on activation; do the same for the AI.
	if (const AActor* T = Target.Get())
	{
		const FVector ToTarget = (T->GetActorLocation() - Me->GetActorLocation()).GetSafeNormal2D();
		if (!ToTarget.IsNearlyZero())
		{
			FRotator Rot = Me->GetActorRotation();
			Rot.Yaw = ToTarget.Rotation().Yaw;
			Me->SetActorRotation(Rot);
		}
	}

	ComboLength = RollComboLength(); // planned swing count; reset below if the press is rejected
	if (UBH_CombatFunctionLibrary::HandleMeleeAttackInput(Me, MeleeClass))
	{
		++StatCombosStarted;
		++StatSwingsFed;
		++StatComboSteps;
		LastObservedStep = 0;
		bAttackStarted = true;
		FeedTimer = SwingFeedInterval;
		SetState(EBH_AIState::Attack);
	}
	else
	{
		// Could not start (stamina / blocked by a state tag): no combo happened, so reset the plan and wait a short
		// beat instead of retrying every tick. No backstep: nothing was swung.
		bAttackStarted = false;
		ComboLength = 0;
		++StatFailedAttackStarts;
		UE_LOG(LogBHCombat, Verbose, TEXT("%s brain: swing start rejected, retry in %.2fs"), *GetNameSafe(Me), FailedAttackRetryDelay);
		ReleaseAttackToken(); // a failed start never keeps the token (BeginRecover would release it too)
		BeginRecover(/*bAllowBackstep*/ false, FailedAttackRetryDelay);
	}
}

void ABH_AIController::TickAttack(float DeltaSeconds)
{
	APawn* Me = GetPawn();
	UAbilitySystemComponent* ASC = GetPawnASC();
	UClass* MeleeClass = FindGrantedAbilityClass(TAG_Ability_Combat_MeleeAttack);
	if (!Me || !ASC || !MeleeClass)
	{
		BeginRecover();
		return;
	}

	const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromClass(MeleeClass);
	if (!Spec || !Spec->IsActive() || StateTime > 8.f)
	{
		bAttackStarted = false;
		BeginRecover();
		return;
	}

	FaceTarget(DeltaSeconds * 0.4f, 10.f);

	FeedTimer -= DeltaSeconds;
	if (FeedTimer > 0.f)
	{
		return;
	}
	FeedTimer = SwingFeedInterval;

	int32 Step = 0;
	for (UGameplayAbility* Instance : Spec->GetAbilityInstances())
	{
		if (const UAH_GA_MeleeAttack_Base* Melee = Cast<UAH_GA_MeleeAttack_Base>(Instance))
		{
			Step = Melee->GetCurrentComboStep();
			break;
		}
	}
	if (Step > LastObservedStep)
	{
		StatComboSteps += Step - LastObservedStep;
		LastObservedStep = Step;
	}

	if (Step + 1 >= ComboLength)
	{
		return; // enough swings: let the last step play out
	}
	if (GetStamina() < MinStaminaToSwing)
	{
		ComboLength = Step + 1; // out of breath: stop feeding
		return;
	}
	if (UBH_CombatFunctionLibrary::HandleMeleeAttackInput(Me, MeleeClass))
	{
		++StatSwingsFed;
	}
}

void ABH_AIController::BeginRecover(bool bAllowBackstep, float OverrideRecoverTime)
{
	ReleaseAttackToken(); // combo finished (or never started): the next attacker may go after the handoff cooldown
	StopMovement();
	RecoverTimeLeft = OverrideRecoverTime >= 0.f ? OverrideRecoverTime : FMath::FRandRange(RecoverTimeMin, FMath::Max(RecoverTimeMin, RecoverTimeMax));
	SetState(EBH_AIState::Recover);

	if (bAllowBackstep && (bAttackStarted || ComboLength > 0))
	{
		const UWorld* World = GetWorld();
		const float Now = World ? World->GetTimeSeconds() : 0.f;
		const bool bOffCooldown = (Now - LastBackstepTime) >= BackstepCooldown;
		const bool bAllowedByChain = !(bNoConsecutiveBacksteps && bLastComboBackstepped);

		bool bBackstepped = false;
		if (bOffCooldown && bAllowedByChain && FMath::FRand() < BackstepChance && GetStamina() >= BackstepMinStamina)
		{
			// No movement input at the moment of the press -> the dodge picks the backstep.
			if (UBH_CombatFunctionLibrary::HandleDodgeInput(GetPawn()))
			{
				++StatBacksteps;
				LastBackstepTime = Now;
				bBackstepped = true;
			}
		}
		// Only combo-ending recoveries update the chain; failed swing starts (bAllowBackstep == false) leave it alone.
		bLastComboBackstepped = bBackstepped;
	}
	bAttackStarted = false;
	ComboLength = 0;
}

void ABH_AIController::TickRecover(float DeltaSeconds)
{
	if (const UAbilitySystemComponent* ASC = GetPawnASC())
	{
		if (ASC->HasMatchingGameplayTag(TAG_State_Combat_Dodging))
		{
			return; // let a backstep finish before the timer runs
		}
	}
	if (RecoverChaseDistance > 0.f && Target.IsValid() && GetDistanceToTarget() > AttackRange + RecoverChaseDistance)
	{
		SetState(EBH_AIState::Approach); // target slipped away: apply pressure instead of idling out the timer
		return;
	}
	FaceTarget(DeltaSeconds, 10.f);
	RecoverTimeLeft -= DeltaSeconds;
	if (RecoverTimeLeft <= 0.f)
	{
		SetState(Target.IsValid() ? EBH_AIState::Approach : EBH_AIState::Idle);
	}
}

// ============================================================================
// Attack tokens / Circle
// ============================================================================

bool ABH_AIController::AcquireAttackToken()
{
	if (!bUseAttackTokens || !UBH_AttackTokenSubsystem::bEnableAttackTokens)
	{
		return true;
	}
	AActor* T = Target.Get();
	UBH_AttackTokenSubsystem* Tokens = UBH_AttackTokenSubsystem::Get(this);
	if (!T || !Tokens)
	{
		return true; // no subsystem (should not happen on the server): never deadlock the AI
	}
	return Tokens->RequestToken(T, this);
}

void ABH_AIController::ReleaseAttackToken()
{
	AActor* T = Target.Get();
	if (UBH_AttackTokenSubsystem* Tokens = T ? UBH_AttackTokenSubsystem::Get(this) : nullptr)
	{
		Tokens->ReleaseToken(T, this);
	}
}

void ABH_AIController::EnterCircle()
{
	StopMovement();
	++StatTokenWaits;
	CircleDir = FMath::RandBool() ? 1.f : -1.f;
	CircleFlipTimeLeft = FMath::FRandRange(CircleFlipTimeMin, FMath::Max(CircleFlipTimeMin, CircleFlipTimeMax));
	CircleBlockedTimer = 0.f;
	CircleGraceTimeLeft = 0.5f;
	FeintTimeLeft = 0.f;
	SetState(EBH_AIState::Circle);
}

void ABH_AIController::TickCircle(float DeltaSeconds)
{
	APawn* Me = GetPawn();
	AActor* T = Target.Get();
	if (!Me || !T)
	{
		SetState(EBH_AIState::Idle);
		return;
	}
	StatCircleTime += DeltaSeconds;

	SetFocus(T);
	if (ABH_CharacterBase* BHPawn = Cast<ABH_CharacterBase>(Me))
	{
		BHPawn->SetAIDesiredGait(EBH_Gait::Walk); // a menacing prowl (SetState resets to Run outside Approach)
	}
	FaceTarget(DeltaSeconds, 10.f);

	// Ask every tick (cheap): the subsystem keeps us a fresh waiter and hands the token to the best-scored one.
	if (AcquireAttackToken())
	{
		SetState(EBH_AIState::Approach); // run in (Approach picks the run gait at this range) and swing
		return;
	}

	const FVector MyLocation = Me->GetActorLocation();
	FVector ToTarget = T->GetActorLocation() - MyLocation;
	ToTarget.Z = 0.0;
	const float Dist = static_cast<float>(ToTarget.Size());
	FVector Dir = ToTarget.GetSafeNormal();
	if (Dir.IsNearlyZero())
	{
		Dir = Me->GetActorForwardVector().GetSafeNormal2D();
	}

	// Flip: on a timer, or when we are not getting anywhere (a wall / another body in the way).
	bool bFlip = false;
	CircleFlipTimeLeft -= DeltaSeconds;
	if (CircleFlipTimeLeft <= 0.f)
	{
		bFlip = true;
	}
	if (CircleGraceTimeLeft > 0.f)
	{
		CircleGraceTimeLeft -= DeltaSeconds;
		CircleBlockedTimer = 0.f;
	}
	else if (FeintTimeLeft <= 0.f)
	{
		CircleBlockedTimer = Me->GetVelocity().Size2D() < CircleBlockedSpeed ? CircleBlockedTimer + DeltaSeconds : 0.f;
		if (CircleBlockedTimer >= CircleBlockedTime)
		{
			bFlip = true;
		}
	}
	if (bFlip)
	{
		CircleDir = -CircleDir;
		CircleFlipTimeLeft = FMath::FRandRange(CircleFlipTimeMin, FMath::Max(CircleFlipTimeMin, CircleFlipTimeMax));
		CircleBlockedTimer = 0.f;
		CircleGraceTimeLeft = 0.5f;
		if (FeintTimeLeft <= 0.f && FMath::FRand() < CircleFeintChance)
		{
			FeintTimeLeft = CircleFeintDuration; // a quick step in and back out; no token, never an attack
		}
	}

	// Feint: half the time toward the target, half back.
	if (FeintTimeLeft > 0.f)
	{
		FeintTimeLeft -= DeltaSeconds;
		const float Phase = FeintTimeLeft > CircleFeintDuration * 0.5f ? 1.f : -1.f;
		Me->AddMovementInput(Dir, Phase);
		return;
	}

	// Strafe along the tangent, nudged back into the [Min, Max] ring.
	const FVector Tangent = FVector::CrossProduct(FVector::UpVector, Dir) * CircleDir;
	float Radial = 0.f; // + toward the target
	if (Dist < CircleRadiusMin)
	{
		Radial = -FMath::Clamp((CircleRadiusMin - Dist) / 60.f, 0.4f, 1.f);
	}
	else if (Dist > CircleRadiusMax)
	{
		Radial = FMath::Clamp((Dist - CircleRadiusMax) / 120.f, 0.4f, 1.f);
	}

	// Separation: do not stack on the other circling AIs.
	FVector Push = FVector::ZeroVector;
	if (CircleSeparationRadius > 0.f && GetWorld())
	{
		for (FConstControllerIterator It = GetWorld()->GetControllerIterator(); It; ++It)
		{
			const ABH_AIController* Other = Cast<ABH_AIController>(It->Get());
			const APawn* OtherPawn = (Other && Other != this) ? Other->GetPawn() : nullptr;
			if (!OtherPawn)
			{
				continue;
			}
			FVector Away = MyLocation - OtherPawn->GetActorLocation();
			Away.Z = 0.0;
			const float OtherDist = static_cast<float>(Away.Size());
			if (OtherDist < CircleSeparationRadius)
			{
				Push += (OtherDist > KINDA_SMALL_NUMBER ? Away / OtherDist : Tangent) * (1.f - OtherDist / CircleSeparationRadius);
			}
		}
	}

	const FVector Move = (Tangent + Dir * Radial + Push * CircleSeparationWeight).GetClampedToMaxSize(1.0);
	if (!Move.IsNearlyZero())
	{
		Me->AddMovementInput(Move.GetSafeNormal(), static_cast<float>(Move.Size()));
	}
}

// ============================================================================
// Tick
// ============================================================================

void ABH_AIController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	APawn* Me = GetPawn();
	const UAbilitySystemComponent* ASC = GetPawnASC();
	if (!Me || !ASC || !Me->HasAuthority())
	{
		return;
	}

	if (bResetHold)
	{
		return; // being walked / teleported home by the reset component
	}

	StateTime += DeltaSeconds;
	DefendCooldownLeft = FMath::Max(0.f, DefendCooldownLeft - DeltaSeconds);

	// Staggered / posture-broken / dead: stand down until it passes.
	const bool bShouldPause = ASC->HasMatchingGameplayTag(TAG_State_Combat_Staggered)
		|| ASC->HasMatchingGameplayTag(TAG_State_Combat_PostureBroken)
		|| ASC->HasMatchingGameplayTag(TAG_State_Combat_Dead);
	if (bShouldPause)
	{
		if (State != EBH_AIState::Paused)
		{
			EnterPaused();
		}
		return;
	}
	if (State == EBH_AIState::Paused)
	{
		SetState(EBH_AIState::Idle);
		ScanTimer = 0.f;
	}

	// Drop a target that died or ran away.
	if (AActor* T = Target.Get())
	{
		if (!IsValidHostile(Cast<APawn>(T), LoseTargetRange))
		{
			if (State == EBH_AIState::Defend)
			{
				EndDefend();
			}
			SetTarget(nullptr);
		}
	}
	else if (Target.IsStale())
	{
		// The target was destroyed: its ASC (and our tag listener) went with it.
		Target.Reset();
		WatchedTargetASC.Reset();
		TargetAttackingHandle.Reset();
		StopMovement();
		ClearFocus(EAIFocusPriority::Gameplay);
		EndDefend();
		SetState(EBH_AIState::Idle);
	}

	// (Re)acquire between fights.
	if (State == EBH_AIState::Idle || State == EBH_AIState::Approach || State == EBH_AIState::Recover || State == EBH_AIState::Circle)
	{
		ScanTimer -= DeltaSeconds;
		if (ScanTimer <= 0.f)
		{
			ScanTimer = TargetScanInterval;
			ScanForTarget();
		}
	}

	switch (State)
	{
	case EBH_AIState::Idle: if (Target.IsValid()) { SetState(EBH_AIState::Approach); } break;
	case EBH_AIState::Approach: TickApproach(DeltaSeconds); break;
	case EBH_AIState::Attack: TickAttack(DeltaSeconds); break;
	case EBH_AIState::Recover: TickRecover(DeltaSeconds); break;
	case EBH_AIState::Defend: TickDefend(DeltaSeconds); break;
	case EBH_AIState::Circle: TickCircle(DeltaSeconds); break;
	default: break;
	}
}
