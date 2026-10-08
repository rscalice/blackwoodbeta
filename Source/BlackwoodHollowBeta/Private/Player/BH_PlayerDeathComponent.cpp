// Blackwood Hollow - player death, replicated ragdoll, revive window, hub respawn, player-to-player revive (implementation)

#include "Player/BH_PlayerDeathComponent.h"
#include "Player/BH_PartyStateSubsystem.h"
#include "Player/BH_RespawnPoint.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystem/Effects/AH_GE_CombatEffects.h"
#include "Characters/BH_CharacterBase.h"
#include "Combat/BH_CombatIdentityComponent.h"
#include "Components/BPC_HeartFragment.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/CapsuleComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Components/SkeletalMeshComponent.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Net/UnrealNetwork.h"
#include "PhysicsEngine/BodyInstance.h"
#include "TimerManager.h"

namespace BH_PlayerDeath_Private
{
	static TAutoConsoleVariable<int32> CVarRagdollSafeCameraAnchor(
		TEXT("bh.Ragdoll.SafeCameraAnchor"),
		1,
		TEXT("1 = while the local player's body is down, keep the camera rig's safe point out of walls / ceilings by lowering the (collision-less) capsule the camera follows. 0 = capsule exactly on the pelvis."),
		ECVF_Default);
}

UBH_PlayerDeathComponent::UBH_PlayerDeathComponent()
{
	// Ticks only while the ragdoll still has to be tracked / corrected, or this pawn is reviving someone (RefreshTickState).
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PostPhysics; // read the physics bodies after the simulation step
	SetIsReplicatedByDefault(true);
}

void UBH_PlayerDeathComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UBH_PlayerDeathComponent, DeathState);
	DOREPLIFETIME(UBH_PlayerDeathComponent, ReviveInfo);
}

// ============================================================================
// Lookup helpers
// ============================================================================

UBH_PlayerDeathComponent* UBH_PlayerDeathComponent::Find(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UBH_PlayerDeathComponent>() : nullptr;
}

bool UBH_PlayerDeathComponent::IsLivingPlayer(const AActor* Actor)
{
	const UBH_PlayerDeathComponent* Comp = Find(Actor);
	if (!Comp || Comp->DeathState.Phase != EBH_DeathPhase::Alive)
	{
		return false;
	}
	const UAbilitySystemComponent* ASC = Comp->GetOwnerASC();
	return !ASC || !ASC->HasMatchingGameplayTag(TAG_State_Combat_Dead);
}

ACharacter* UBH_PlayerDeathComponent::GetCharacterOwner() const
{
	return Cast<ACharacter>(GetOwner());
}

USkeletalMeshComponent* UBH_PlayerDeathComponent::GetOwnerMesh() const
{
	const ACharacter* Char = GetCharacterOwner();
	return Char ? Char->GetMesh() : nullptr;
}

UAbilitySystemComponent* UBH_PlayerDeathComponent::GetOwnerASC() const
{
	return UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner());
}

float UBH_PlayerDeathComponent::GetCapsuleHalfHeight() const
{
	const ACharacter* Char = GetCharacterOwner();
	const UCapsuleComponent* Capsule = Char ? Char->GetCapsuleComponent() : nullptr;
	return Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 88.f;
}

FVector UBH_PlayerDeathComponent::GetBodyRestLocation() const
{
	if (DeathState.bBodySettled)
	{
		return DeathState.BodyRestLocation;
	}
	const AActor* OwnerActor = GetOwner();
	return OwnerActor ? OwnerActor->GetActorLocation() : FVector::ZeroVector;
}

FString UBH_PlayerDeathComponent::GetRespawnHubLabel() const
{
	return DeathState.HubName.IsNone() ? FString() : FName::NameToDisplayString(DeathState.HubName.ToString(), false);
}

bool UBH_PlayerDeathComponent::GetPelvisState(FVector& OutLocation, FVector& OutVelocity, FQuat& OutRotation) const
{
	const USkeletalMeshComponent* SkelMesh = GetOwnerMesh();
	const FBodyInstance* Body = SkelMesh ? SkelMesh->GetBodyInstance(PelvisBoneName) : nullptr;
	if (!Body || !Body->IsValidBodyInstance())
	{
		return false;
	}
	const FTransform BodyTransform = Body->GetUnrealWorldTransform();
	OutLocation = BodyTransform.GetLocation();
	OutRotation = BodyTransform.GetRotation();
	OutVelocity = Body->GetUnrealWorldVelocity();
	return true;
}

// ============================================================================
// Lifecycle
// ============================================================================

void UBH_PlayerDeathComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	if (OwnerActor->HasAuthority())
	{
		// Where the GameMode first put this pawn: the last-resort hub respawn destination.
		InitialSpawnTransform = OwnerActor->GetActorTransform();

		if (UAbilitySystemComponent* ASC = GetOwnerASC())
		{
			DeadTagHandle = ASC->RegisterGameplayTagEvent(TAG_State_Combat_Dead, EGameplayTagEventType::NewOrRemoved)
				.AddUObject(this, &UBH_PlayerDeathComponent::OnDeadTagChanged);
		}
	}

	// A pawn that becomes relevant (or joins) while the state is already Dead starts its ragdoll here.
	ApplyState();

	// Local player: keep the revive mapping context in step with "a downed partner is in range" (no-op on non-local pawns).
	if (const UWorld* World = GetWorld())
	{
		if (World->GetNetMode() != NM_DedicatedServer)
		{
			World->GetTimerManager().SetTimer(ReviveContextTimer, this, &UBH_PlayerDeathComponent::UpdateReviveInputContext, FMath::Max(0.02f, ReviveContextPollInterval), true);
		}
	}
}

void UBH_PlayerDeathComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (DeadTagHandle.IsValid())
	{
		if (UAbilitySystemComponent* ASC = GetOwnerASC())
		{
			ASC->RegisterGameplayTagEvent(TAG_State_Combat_Dead, EGameplayTagEventType::NewOrRemoved).Remove(DeadTagHandle);
		}
		DeadTagHandle.Reset();
	}

	CancelReviveAttempt();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ReviveContextTimer);
		World->GetTimerManager().ClearTimer(GetUpTimer);
	}
	RemoveReviveInputContext();
	RestoreRootMotionMode();

	Super::EndPlay(EndPlayReason);
}

void UBH_PlayerDeathComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bRagdolling)
	{
		TickRagdoll(DeltaTime);
	}
	if (ReviveTargetPawn.IsValid())
	{
		TickRevive(DeltaTime);
	}
	RefreshTickState();
}

void UBH_PlayerDeathComponent::RefreshTickState()
{
	const bool bNeedTick = (bRagdolling && !bBodyCorrected) || ReviveTargetPawn.IsValid();
	SetComponentTickEnabled(bNeedTick);
}

// ============================================================================
// Replicated state -> local effects (every machine)
// ============================================================================

void UBH_PlayerDeathComponent::OnRep_DeathState()
{
	ApplyState();
}

void UBH_PlayerDeathComponent::OnRep_ReviveInfo()
{
	BroadcastReviveProgress();
}

void UBH_PlayerDeathComponent::ApplyState()
{
	if (!HasBegunPlay())
	{
		return; // BeginPlay applies the initial state
	}

	const bool bWasDown = AppliedPhase != EBH_DeathPhase::Alive;
	const bool bIsDown = DeathState.Phase != EBH_DeathPhase::Alive;
	const bool bSameDeath = bWasDown && bIsDown && DeathState.Epoch == AppliedEpoch;

	if (bWasDown && !bSameDeath)
	{
		ExitDeathLocal(); // revived / respawned (or revived and killed again inside one update)
	}
	if (bIsDown && !bSameDeath)
	{
		EnterDeathLocal();
	}

	const EBH_DeathPhase OldPhase = AppliedPhase;
	AppliedPhase = DeathState.Phase;
	AppliedEpoch = DeathState.Epoch;
	if (OldPhase != AppliedPhase)
	{
		OnDeathPhaseChanged.Broadcast(AppliedPhase, OldPhase);
	}
	RefreshTickState();
	UpdateReviveInputContext(); // dying drops the revive input at once; a partner's phase change re-evaluates it before the next poll
}

void UBH_PlayerDeathComponent::EnterDeathLocal()
{
	ACharacter* Char = GetCharacterOwner();
	if (!Char)
	{
		return;
	}
	USkeletalMeshComponent* SkelMesh = Char->GetMesh();
	UCharacterMovementComponent* MoveComp = Char->GetCharacterMovement();

	// Dying while still getting up from a previous death: drop that first.
	if (bGettingUp)
	{
		EndGetUp();
	}
	RestoreRootMotionMode();

	bRagdolling = true;
	bBodyCorrected = false;
	SlowTime = 0.f;
	RagdollStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const FVector EntryVelocity = MoveComp ? MoveComp->Velocity : Char->GetVelocity(); // read before the CMC is stopped

	// Server: remember which way the belly faces while the pawn is still upright (decides face up / down once the body lies).
	bBellyValid = false;
	if (Char->HasAuthority() && SkelMesh && SkelMesh->GetBoneIndex(PelvisBoneName) != INDEX_NONE)
	{
		const FTransform PelvisTransform = SkelMesh->GetSocketTransform(PelvisBoneName, RTS_World);
		BellyLocal = PelvisTransform.InverseTransformVectorNoScale(Char->GetActorForwardVector());
		bBellyValid = true;
	}

	// The shared ragdoll: CMC off, movement replication off (server), capsule and weapon colliders off, mesh DETACHED from the capsule
	// and simulating its physics asset with the pawn's (capped) velocity.
	if (ABH_CharacterBase* BaseChar = Cast<ABH_CharacterBase>(Char))
	{
		BaseChar->StartRagdollLocal(EntryVelocity);
	}
	else
	{
		UE_LOG(LogBHCombat, Warning, TEXT("%s: the death component needs an ABH_CharacterBase owner to ragdoll."), *GetNameSafe(Char));
	}

	// Camera: stays on the own body (the capsule follows the pelvis in TickRagdoll).
	// TODO(spectate): switch the view target to a partner here and back in ExitDeathLocal.
	K2_OnRagdollStarted();
}

void UBH_PlayerDeathComponent::ExitDeathLocal()
{
	ACharacter* Char = GetCharacterOwner();
	bRagdolling = false;
	bBodyCorrected = false;
	if (!Char)
	{
		return;
	}
	UCharacterMovementComponent* MoveComp = Char->GetCharacterMovement();

	// Stop the simulation, hang the mesh back on the capsule and put the capsule where the pawn comes back
	// (collision still off until then: no depenetration pop).
	const FTransform ReturnTransform(FRotator(0.f, DeathState.GetUpYaw, 0.f), FVector(DeathState.ReviveLocation));
	if (ABH_CharacterBase* BaseChar = Cast<ABH_CharacterBase>(Char))
	{
		BaseChar->StopRagdollLocal(&ReturnTransform);
	}
	else
	{
		Char->SetActorLocationAndRotation(ReturnTransform.GetLocation(), ReturnTransform.Rotator(), false, nullptr, ETeleportType::TeleportPhysics);
	}

	if (MoveComp)
	{
		MoveComp->Velocity = FVector::ZeroVector;
		MoveComp->SetComponentTickEnabled(true);
		MoveComp->SetMovementMode(MOVE_Falling); // lands on the floor next tick (same as GASP's Ragdoll_End)
	}

	StartGetUp(DeathState.bBodyFaceUp);
}

void UBH_PlayerDeathComponent::TickRagdoll(float DeltaTime)
{
	ACharacter* Char = GetCharacterOwner();
	const UWorld* World = GetWorld();
	if (!Char || !World)
	{
		return;
	}

	FVector PelvisLocation = FVector::ZeroVector;
	FVector PelvisVelocity = FVector::ZeroVector;
	FQuat PelvisRotation = FQuat::Identity;
	const bool bHaveBody = GetPelvisState(PelvisLocation, PelvisVelocity, PelvisRotation);

	const double Elapsed = World->GetTimeSeconds() - RagdollStartTime;
	if (!bHaveBody || PelvisVelocity.Size() < SettleSpeedThreshold)
	{
		SlowTime += DeltaTime; // no physics body (dedicated server without mesh physics): nothing moves, treat as still
	}
	else
	{
		SlowTime = 0.f;
	}
	const bool bLocallySettled = (Elapsed >= SettleMinSeconds && SlowTime >= SettleHoldSeconds) || Elapsed >= SettleTimeout;

	// Until the rest point is applied the capsule follows the pelvis, so the camera stays on the body. The mesh is detached from the
	// capsule (StartRagdollLocal), so this only moves the capsule (and the camera on it), never the simulating bodies.
	if (!bBodyCorrected && bHaveBody)
	{
		Char->SetActorLocation(ComputeCameraAnchor(PelvisLocation), false, nullptr, ETeleportType::TeleportPhysics);
	}

	if (Char->HasAuthority())
	{
		if (!DeathState.bBodySettled && bLocallySettled)
		{
			SampleBodyRest();
		}
	}
	else if (DeathState.bBodySettled && !bBodyCorrected && bLocallySettled)
	{
		ApplyBodyCorrection();
	}
}

void UBH_PlayerDeathComponent::ApplyBodyCorrection()
{
	ACharacter* Char = GetCharacterOwner();
	USkeletalMeshComponent* SkelMesh = GetOwnerMesh();
	if (!Char)
	{
		return;
	}

	FVector PelvisLocation = FVector::ZeroVector;
	FVector PelvisVelocity = FVector::ZeroVector;
	FQuat PelvisRotation = FQuat::Identity;
	if (SkelMesh && GetPelvisState(PelvisLocation, PelvisVelocity, PelvisRotation))
	{
		// This machine's ragdoll came to rest somewhere else: shift every body by the difference (keeps the pose).
		const FVector Delta = FVector(DeathState.BodyRestLocation) - PelvisLocation;
		if (Delta.Size() > ClientCorrectionTolerance)
		{
			for (FBodyInstance* Body : SkelMesh->Bodies)
			{
				if (Body && Body->IsValidBodyInstance())
				{
					FTransform BodyTransform = Body->GetUnrealWorldTransform();
					BodyTransform.AddToTranslation(Delta);
					Body->SetBodyTransform(BodyTransform, ETeleportType::TeleportPhysics);
				}
			}
			SkelMesh->SetAllPhysicsLinearVelocity(FVector::ZeroVector);
			SkelMesh->SetAllPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		}
	}

	Char->SetActorLocation(ComputeCameraAnchor(FVector(DeathState.BodyRestLocation)), false, nullptr, ETeleportType::TeleportPhysics);
	bBodyCorrected = true;
}

FVector UBH_PlayerDeathComponent::ComputeCameraAnchor(const FVector& BodyLocation) const
{
	const ACharacter* Char = GetCharacterOwner();
	const UWorld* World = GetWorld();
	if (!Char || !World || !Char->IsLocallyControlled()
		|| BH_PlayerDeath_Private::CVarRagdollSafeCameraAnchor.GetValueOnGameThread() == 0
		|| CameraRigSafeOffset.IsNearlyZero())
	{
		return BodyLocation;
	}

	// Where the rig wants its safe point: capsule + offset in Pawn space (yaw only, the capsule is never pitched).
	const FVector SafeOffsetWorld = Char->GetActorRotation().RotateVector(CameraRigSafeOffset);
	const FVector Start = BodyLocation;
	const FVector End = BodyLocation + SafeOffsetWorld;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(BHRagdollCameraAnchor), false, Char);
	TArray<AActor*> CarriedActors;
	Char->GetAttachedActors(CarriedActors, /*bResetArray*/ true, /*bRecursivelyIncludeAttachedActors*/ true);
	Params.AddIgnoredActors(CarriedActors);

	FVector SafePoint = End;
	FHitResult Hit;
	if (World->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_Camera, FCollisionShape::MakeSphere(FMath::Max(0.f, CameraProbeRadius)), Params))
	{
		if (Hit.bStartPenetrating)
		{
			// The body itself lies inside (or against) camera-blocking geometry: push the safe point out along the depenetration normal.
			SafePoint = Start + Hit.Normal * (Hit.PenetrationDepth + CameraAnchorMargin);
		}
		else
		{
			// Something (a ceiling, an overhang) sits between the body and the safe point: stop short of it.
			const float SafeDistance = FMath::Max(0.f, Hit.Distance - CameraAnchorMargin);
			SafePoint = Start + (End - Start).GetSafeNormal() * SafeDistance;
		}
	}
	return SafePoint - SafeOffsetWorld;
}

// ============================================================================
// Get-up (every machine)
// ============================================================================

void UBH_PlayerDeathComponent::StartGetUp(bool bFaceUp)
{
	USkeletalMeshComponent* SkelMesh = GetOwnerMesh();
	UAnimInstance* Anim = SkelMesh ? SkelMesh->GetAnimInstance() : nullptr;
	UAnimMontage* Montage = (bFaceUp ? GetUpFaceUpMontage : GetUpFaceDownMontage).LoadSynchronous();

	if (Anim && Montage)
	{
		// The capsule is already where the pawn stands; the get-up must not slide it (the montages carry root motion).
		Anim->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);
		bRootMotionOverridden = true;
		Anim->Montage_Play(Montage, 1.f);
		GetUpMontagePlaying = Montage;
	}
	else if (!Montage)
	{
		UE_LOG(LogBHCombat, Warning, TEXT("%s: no get-up montage (%s), the pawn just stands up."), *GetNameSafe(GetOwner()), bFaceUp ? TEXT("face up") : TEXT("face down"));
	}

	UWorld* World = GetWorld();
	if (World && GetUpDurationSeconds > 0.f)
	{
		bGettingUp = true;
		World->GetTimerManager().SetTimer(GetUpTimer, this, &UBH_PlayerDeathComponent::EndGetUp, GetUpDurationSeconds, false);
	}
	K2_OnGetUpStarted(bFaceUp);
}

void UBH_PlayerDeathComponent::EndGetUp()
{
	UWorld* World = GetWorld();
	if (World)
	{
		World->GetTimerManager().ClearTimer(GetUpTimer);
	}
	bGettingUp = false;

	USkeletalMeshComponent* SkelMesh = GetOwnerMesh();
	UAnimInstance* Anim = SkelMesh ? SkelMesh->GetAnimInstance() : nullptr;
	if (Anim)
	{
		if (UAnimMontage* Montage = GetUpMontagePlaying.Get())
		{
			constexpr float BlendOutSeconds = 0.25f;
			Anim->Montage_Stop(BlendOutSeconds, Montage);
			// Root motion stays ignored while the montage blends out, then the mesh's normal mode comes back.
			if (World && bRootMotionOverridden)
			{
				World->GetTimerManager().SetTimer(RootMotionRestoreTimer, this, &UBH_PlayerDeathComponent::RestoreRootMotionMode, BlendOutSeconds + 0.05f, false);
			}
		}
		else
		{
			RestoreRootMotionMode();
		}
	}
	GetUpMontagePlaying.Reset();
}

void UBH_PlayerDeathComponent::RestoreRootMotionMode()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RootMotionRestoreTimer);
	}
	if (!bRootMotionOverridden)
	{
		return;
	}
	bRootMotionOverridden = false;
	USkeletalMeshComponent* SkelMesh = GetOwnerMesh();
	if (UAnimInstance* Anim = SkelMesh ? SkelMesh->GetAnimInstance() : nullptr)
	{
		Anim->SetRootMotionMode(PostGetUpRootMotionMode);
	}
}

// ============================================================================
// Server: death
// ============================================================================

void UBH_PlayerDeathComponent::OnDeadTagChanged(const FGameplayTag Tag, int32 NewCount)
{
	const AActor* OwnerActor = GetOwner();
	if (NewCount <= 0 || !OwnerActor || !OwnerActor->HasAuthority() || DeathState.Phase != EBH_DeathPhase::Alive || bDeathPending)
	{
		return;
	}
	// The tag is added in the middle of the attribute set's health-zero handling: start the death on the next tick.
	bDeathPending = true;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(this, &UBH_PlayerDeathComponent::BeginDeath);
	}
}

void UBH_PlayerDeathComponent::BeginDeath()
{
	bDeathPending = false;

	AActor* OwnerActor = GetOwner();
	UAbilitySystemComponent* ASC = GetOwnerASC();
	if (!OwnerActor || !OwnerActor->HasAuthority() || !ASC || DeathState.Phase != EBH_DeathPhase::Alive)
	{
		return;
	}
	if (!ASC->HasMatchingGameplayTag(TAG_State_Combat_Dead))
	{
		return; // revived / reset in the meantime
	}
	if (UBH_CombatIdentityComponent::Find(OwnerActor))
	{
		return; // an AI combatant on the same base class: it has its own death handling
	}

	ASC->CancelAllAbilities();
	CancelReviveAttempt();
	SetReviveInfo(nullptr, 0);

	FBH_DeathRepState NewState;
	NewState.Phase = EBH_DeathPhase::Dead;
	NewState.Epoch = static_cast<uint8>(DeathState.Epoch + 1);
	// Until the body settles, anyone who becomes relevant sees the pawn where it fell.
	NewState.BodyRestLocation = OwnerActor->GetActorLocation();
	NewState.ReviveLocation = OwnerActor->GetActorLocation();
	NewState.GetUpYaw = OwnerActor->GetActorRotation().Yaw;
	DeathState = NewState;
	CommitState();

	UE_LOG(LogBHCombat, Log, TEXT("%s died (revive state Dead)."), *GetNameSafe(OwnerActor));

	if (UBH_PartyStateSubsystem* Party = UBH_PartyStateSubsystem::Get(this))
	{
		Party->NotifyPlayerStateChanged();
	}
}

void UBH_PlayerDeathComponent::CommitState()
{
	if (AActor* OwnerActor = GetOwner())
	{
		OwnerActor->ForceNetUpdate();
	}
	ApplyState(); // the server (and a listen-server host) gets no RepNotify
}

void UBH_PlayerDeathComponent::SampleBodyRest()
{
	ACharacter* Char = GetCharacterOwner();
	const UWorld* World = GetWorld();
	if (!Char || !World || !Char->HasAuthority() || DeathState.Phase == EBH_DeathPhase::Alive)
	{
		return;
	}
	const USkeletalMeshComponent* SkelMesh = GetOwnerMesh();

	FVector PelvisLocation = Char->GetActorLocation();
	FVector PelvisVelocity = FVector::ZeroVector;
	FQuat PelvisRotation = FQuat::Identity;
	const bool bHaveBody = GetPelvisState(PelvisLocation, PelvisVelocity, PelvisRotation);
	if (!bHaveBody)
	{
		UE_LOG(LogBHCombat, Warning, TEXT("%s: no pelvis physics body on the server (dedicated server without mesh physics?), the body rests at the capsule."), *GetNameSafe(Char));
	}

	// Which way the body lies: belly up, and the direction the get-up should face.
	bool bFaceUp = false;
	float FacingYaw = Char->GetActorRotation().Yaw;
	if (bHaveBody)
	{
		if (bBellyValid)
		{
			bFaceUp = PelvisRotation.RotateVector(BellyLocal).Z > 0.f;
		}
		const FBodyInstance* HeadBody = SkelMesh ? SkelMesh->GetBodyInstance(HeadBoneName) : nullptr;
		if (HeadBody && HeadBody->IsValidBodyInstance())
		{
			FVector HeadDirection = HeadBody->GetUnrealWorldTransform().GetLocation() - PelvisLocation;
			HeadDirection.Z = 0.f;
			if (!HeadDirection.IsNearlyZero(1.f))
			{
				// Face down: the pawn gets up facing where the head points. Face up: it sits up toward the feet.
				const FVector Facing = bFaceUp ? -HeadDirection : HeadDirection;
				FacingYaw = Facing.Rotation().Yaw;
			}
		}
	}

	// Floor under the body, then the capsule position of a standing pawn there.
	float GroundZ = PelvisLocation.Z - 20.f;
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(BHDeathGround), false, Char);
		FHitResult Hit;
		const FVector Start = PelvisLocation + FVector(0.f, 0.f, GroundTraceUp);
		const FVector End = PelvisLocation - FVector(0.f, 0.f, GroundTraceDown);
		if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
		{
			GroundZ = Hit.ImpactPoint.Z;
		}
	}

	DeathState.BodyRestLocation = PelvisLocation;
	DeathState.ReviveLocation = FVector(PelvisLocation.X, PelvisLocation.Y, GroundZ + GetCapsuleHalfHeight() + 2.f);
	DeathState.bBodyFaceUp = bFaceUp;
	DeathState.GetUpYaw = FacingYaw;
	DeathState.bBodySettled = true;

	// The server's capsule rests at the body from now on (relevancy, late joiners). A host's own pawn is lowered under low ceilings so its camera stays clear.
	Char->SetActorLocation(ComputeCameraAnchor(PelvisLocation), false, nullptr, ETeleportType::TeleportPhysics);
	bBodyCorrected = true;

	CommitState();
	TryOpenReviveWindow();
}

void UBH_PlayerDeathComponent::TryOpenReviveWindow()
{
	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority() || DeathState.Phase != EBH_DeathPhase::Dead || !DeathState.bBodySettled)
	{
		return;
	}

	const UBH_PartyStateSubsystem* Party = UBH_PartyStateSubsystem::Get(this);
	const bool bAlone = !Party || !Party->HasOtherLivingPlayer(this); // solo (or the whole party is down): nobody to wait for
	if (Party && Party->IsPartyInCombat() && !bAlone)
	{
		return;
	}

	FVector Destination = FVector::ZeroVector;
	float DestinationYaw = 0.f;
	FName HubName;
	ResolveRespawnDestination(Destination, DestinationYaw, HubName, /*bLogFallback*/ false);

	DeathState.Phase = EBH_DeathPhase::AwaitingChoice;
	DeathState.HubName = HubName;
	CommitState();

	UE_LOG(LogBHCombat, Log, TEXT("%s: revive window open (hub '%s')."), *GetNameSafe(OwnerActor), *HubName.ToString());
}

// ============================================================================
// The dead player's choice
// ============================================================================

void UBH_PlayerDeathComponent::RequestRespawnAtHub()
{
	const APawn* PawnOwner = Cast<APawn>(GetOwner());
	if (!PawnOwner || !PawnOwner->IsLocallyControlled() || !IsWindowOpen())
	{
		return;
	}
	ServerRespawnAtHub();
}

void UBH_PlayerDeathComponent::RequestWaitForRevive()
{
	const APawn* PawnOwner = Cast<APawn>(GetOwner());
	if (!PawnOwner || !PawnOwner->IsLocallyControlled() || DeathState.Phase != EBH_DeathPhase::AwaitingChoice)
	{
		return;
	}
	ServerWaitForRevive();
}

void UBH_PlayerDeathComponent::ServerRespawnAtHub_Implementation()
{
	if (!IsWindowOpen())
	{
		return;
	}
	FVector Destination = FVector::ZeroVector;
	float DestinationYaw = 0.f;
	FName HubName;
	ResolveRespawnDestination(Destination, DestinationYaw, HubName, /*bLogFallback*/ true);
	ReturnToLife(Destination, DestinationYaw, RespawnHealthPercent);
}

void UBH_PlayerDeathComponent::ServerWaitForRevive_Implementation()
{
	if (DeathState.Phase != EBH_DeathPhase::AwaitingChoice)
	{
		return;
	}
	DeathState.Phase = EBH_DeathPhase::WaitingForRevive;
	CommitState();
}

bool UBH_PlayerDeathComponent::ResolveRespawnDestination(FVector& OutLocation, float& OutYaw, FName& OutHubName, bool bLogFallback) const
{
	const AActor* OwnerActor = GetOwner();
	const UWorld* World = GetWorld();
	const float HalfHeight = GetCapsuleHalfHeight();
	const FVector From = DeathState.bBodySettled ? FVector(DeathState.BodyRestLocation) : (OwnerActor ? OwnerActor->GetActorLocation() : FVector::ZeroVector);

	// Spread simultaneous respawns on one point by the player's slot.
	int32 SlotIndex = 0;
	if (const APawn* PawnOwner = Cast<APawn>(OwnerActor))
	{
		if (const AGameStateBase* GameState = World ? World->GetGameState() : nullptr)
		{
			SlotIndex = FMath::Max(0, GameState->PlayerArray.IndexOfByKey(PawnOwner->GetPlayerState()));
		}
	}

	// 1) The nearest hub respawn point.
	if (const UBH_RespawnPointSubsystem* Registry = UBH_RespawnPointSubsystem::Get(this))
	{
		if (const ABH_RespawnPoint* Point = Registry->FindNearest(From))
		{
			OutLocation = Point->ComputeSpawnLocation(HalfHeight, SlotIndex);
			OutYaw = Point->GetSpawnRotation().Yaw;
			OutHubName = Point->HubName;
			return true;
		}
	}

	// 2) Any PlayerStart (nearest to the body).
	const FName FallbackHubName = TEXT("Start");
	if (World)
	{
		const APlayerStart* BestStart = nullptr;
		double BestDistSq = TNumericLimits<double>::Max();
		for (TActorIterator<APlayerStart> It(World); It; ++It)
		{
			const double DistSq = FVector::DistSquared(It->GetActorLocation(), From);
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				BestStart = *It;
			}
		}
		if (BestStart)
		{
			if (bLogFallback)
			{
				UE_LOG(LogBHCombat, Warning, TEXT("%s: no ABH_RespawnPoint in the level, respawning at PlayerStart '%s'. Place an ABH_RespawnPoint at the hub."), *GetNameSafe(OwnerActor), *BestStart->GetName());
			}
			OutLocation = BestStart->GetActorLocation() + FVector(0.f, 0.f, HalfHeight + 2.f);
			OutYaw = BestStart->GetActorRotation().Yaw;
			OutHubName = FallbackHubName;
			return false;
		}
	}

	// 3) Where the GameMode first put this pawn (no respawn point and no PlayerStart exist).
	if (bLogFallback)
	{
		UE_LOG(LogBHCombat, Warning, TEXT("%s: no ABH_RespawnPoint and no PlayerStart in the level, respawning where the pawn first spawned. Place an ABH_RespawnPoint at the hub."), *GetNameSafe(OwnerActor));
	}
	OutLocation = InitialSpawnTransform.GetLocation();
	OutYaw = InitialSpawnTransform.GetRotation().Rotator().Yaw;
	OutHubName = FallbackHubName;
	return false;
}

// ============================================================================
// Server: back to life (hub respawn and revive both end here)
// ============================================================================

void UBH_PlayerDeathComponent::ReviveInPlace(float HealthFraction)
{
	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority() || DeathState.Phase == EBH_DeathPhase::Alive)
	{
		return;
	}
	if (!DeathState.bBodySettled)
	{
		SampleBodyRest(); // revived before the body rested (debug command): use where it is now
	}
	ReturnToLife(FVector(DeathState.ReviveLocation), DeathState.GetUpYaw, HealthFraction);
}

void UBH_PlayerDeathComponent::ReturnToLife(const FVector& Location, float Yaw, float HealthFraction)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority() || DeathState.Phase == EBH_DeathPhase::Alive)
	{
		return;
	}

	// Nobody keeps reviving a body that is already standing up.
	if (APawn* CurrentReviver = ReviveInfo.Reviver)
	{
		if (UBH_PlayerDeathComponent* ReviverComp = Find(CurrentReviver))
		{
			ReviverComp->CancelReviveAttempt();
		}
	}
	SetReviveInfo(nullptr, 0);

	if (UAbilitySystemComponent* ASC = GetOwnerASC())
	{
		if (const UAH_AttributeSet* Set = ASC->GetSet<UAH_AttributeSet>())
		{
			const float Fraction = FMath::Clamp(HealthFraction, 0.01f, 1.f);
			ASC->SetNumericAttributeBase(UAH_AttributeSet::GetHealthAttribute(), FMath::Max(1.f, Set->GetMaxHealth() * Fraction));
			ASC->SetNumericAttributeBase(UAH_AttributeSet::GetPostureAttribute(), Set->GetMaxPosture());
			ASC->SetNumericAttributeBase(UAH_AttributeSet::GetStaminaAttribute(), Set->GetMaxStamina());
		}
		// Death-only state (the same tags UBH_CombatIdentityComponent::ResetAfterDeath clears).
		ASC->SetLooseGameplayTagCount(TAG_State_Combat_Dead, 0, EGameplayTagReplicationState::TagOnly);
		ASC->SetLooseGameplayTagCount(TAG_State_Combat_PostureBroken, 0, EGameplayTagReplicationState::TagOnly);
	}
	if (UBPC_HeartFragment* HeartFragment = OwnerActor->FindComponentByClass<UBPC_HeartFragment>())
	{
		HeartFragment->ResetBlight(); // meter to 0 and Blight Rot removed
	}

	FBH_DeathRepState NewState = DeathState;
	NewState.Phase = EBH_DeathPhase::Alive;
	NewState.Epoch = static_cast<uint8>(DeathState.Epoch + 1);
	NewState.bBodySettled = false;
	NewState.ReviveLocation = Location;
	NewState.GetUpYaw = Yaw;
	NewState.HubName = NAME_None;
	DeathState = NewState;
	CommitState();

	UE_LOG(LogBHCombat, Log, TEXT("%s is back on its feet (health %.0f%%)."), *GetNameSafe(OwnerActor), FMath::Clamp(HealthFraction, 0.01f, 1.f) * 100.f);

	if (UBH_PartyStateSubsystem* Party = UBH_PartyStateSubsystem::Get(this))
	{
		Party->NotifyPlayerStateChanged();
	}
}

// ============================================================================
// Reviving another player
// ============================================================================

bool UBH_PlayerDeathComponent::CanBeRevivedBy(const UBH_PlayerDeathComponent* Reviver) const
{
	if (!IsWindowOpen() || !DeathState.bBodySettled || !Reviver)
	{
		return false;
	}
	return ReviveInfo.Reviver == nullptr || ReviveInfo.Reviver.Get() == Reviver->GetOwner();
}

// ----------------------------------------------------------------------------
// Revive input context (local player only)
// ----------------------------------------------------------------------------

void UBH_PlayerDeathComponent::UpdateReviveInputContext()
{
	const APawn* PawnOwner = Cast<APawn>(GetOwner());
	const ABH_CharacterBase* BaseChar = Cast<ABH_CharacterBase>(GetOwner());

	bool bWantContext = PawnOwner && BaseChar && !BaseChar->ReviveMappingContext.IsNull()
		&& PawnOwner->IsLocallyControlled()
		&& IsLivingPlayer(PawnOwner) // a dead player cannot revive
		&& FindReviveCandidate() != nullptr;

	if (bWantContext && bReviveContextOnlyOutOfCombat)
	{
		const UBH_PartyStateSubsystem* Party = UBH_PartyStateSubsystem::Get(this);
		bWantContext = !Party || !Party->IsPartyInCombat(); // the server refuses revives in combat: X must stay Shield Bash
	}

	UEnhancedInputLocalPlayerSubsystem* CurrentSubsystem = nullptr;
	if (bWantContext)
	{
		const APlayerController* LocalPC = Cast<APlayerController>(PawnOwner->GetController());
		const ULocalPlayer* LocalPlayerInfo = LocalPC ? LocalPC->GetLocalPlayer() : nullptr;
		CurrentSubsystem = LocalPlayerInfo ? LocalPlayerInfo->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
		bWantContext = CurrentSubsystem != nullptr;
	}

	// Remove when no longer wanted, or when the local player changed under us (re-possession by another local player).
	if (bReviveContextActive && (!bWantContext || ReviveContextSubsystem.Get() != CurrentSubsystem))
	{
		RemoveReviveInputContext();
	}
	if (bWantContext && !bReviveContextActive)
	{
		AddReviveInputContext(CurrentSubsystem);
	}
}

void UBH_PlayerDeathComponent::AddReviveInputContext(UEnhancedInputLocalPlayerSubsystem* InputSubsystem)
{
	const ABH_CharacterBase* BaseChar = Cast<ABH_CharacterBase>(GetOwner());
	if (!InputSubsystem || !BaseChar || bReviveContextActive)
	{
		return;
	}
	UInputMappingContext* ContextAsset = BaseChar->ReviveMappingContext.LoadSynchronous();
	if (!ContextAsset)
	{
		if (!bWarnedMissingReviveContext)
		{
			bWarnedMissingReviveContext = true;
			UE_LOG(LogBHCombat, Warning, TEXT("%s: ReviveMappingContext (%s) could not be loaded, revive input will not take priority."), *GetNameSafe(GetOwner()), *BaseChar->ReviveMappingContext.ToString());
		}
		return;
	}

	// Keys already held when the context changes are only ignored if gamepad X is one of them (so a held X does not start a revive by accident);
	// otherwise held sprint / block keys keep working across the change.
	const ULocalPlayer* LocalPlayerInfo = InputSubsystem->GetLocalPlayer();
	const APlayerController* LocalPC = LocalPlayerInfo ? LocalPlayerInfo->GetPlayerController(GetWorld()) : nullptr;
	FModifyContextOptions ContextOptions;
	ContextOptions.bIgnoreAllPressedKeysUntilRelease = LocalPC && LocalPC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Left);

	InputSubsystem->AddMappingContext(ContextAsset, BaseChar->ReviveMappingPriority, ContextOptions);
	ReviveContextSubsystem = InputSubsystem;
	ReviveContextAsset = ContextAsset;
	bReviveContextActive = true;
}

void UBH_PlayerDeathComponent::RemoveReviveInputContext()
{
	if (!bReviveContextActive)
	{
		return;
	}
	bReviveContextActive = false;

	UEnhancedInputLocalPlayerSubsystem* InputSubsystem = ReviveContextSubsystem.Get();
	UInputMappingContext* ContextAsset = ReviveContextAsset.Get();
	ReviveContextSubsystem.Reset();
	ReviveContextAsset.Reset();
	if (!InputSubsystem || !ContextAsset)
	{
		return;
	}

	// X still held when the revive context goes (revive finished / partner out of range): ignore it until release so it does not fire Shield Bash.
	const ULocalPlayer* LocalPlayerInfo = InputSubsystem->GetLocalPlayer();
	const APlayerController* LocalPC = LocalPlayerInfo ? LocalPlayerInfo->GetPlayerController(GetWorld()) : nullptr;
	FModifyContextOptions ContextOptions;
	ContextOptions.bIgnoreAllPressedKeysUntilRelease = LocalPC && LocalPC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Left);

	InputSubsystem->RemoveMappingContext(ContextAsset, ContextOptions);
}

APawn* UBH_PlayerDeathComponent::FindReviveCandidate() const
{
	const AActor* OwnerActor = GetOwner();
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	if (!OwnerActor || !GameState || !IsLivingPlayer(OwnerActor))
	{
		return nullptr;
	}

	APawn* Best = nullptr;
	float BestDistance = ReviveRange;
	for (const APlayerState* PlayerStateEntry : GameState->PlayerArray)
	{
		APawn* Candidate = PlayerStateEntry ? PlayerStateEntry->GetPawn() : nullptr;
		if (!Candidate || Candidate == OwnerActor)
		{
			continue;
		}
		const UBH_PlayerDeathComponent* CandidateComp = Find(Candidate);
		if (!CandidateComp || !CandidateComp->CanBeRevivedBy(this))
		{
			continue;
		}
		const float Distance = static_cast<float>(FVector::Dist(OwnerActor->GetActorLocation(), CandidateComp->GetBodyRestLocation()));
		if (Distance <= BestDistance)
		{
			BestDistance = Distance;
			Best = Candidate;
		}
	}
	return Best;
}

bool UBH_PlayerDeathComponent::StartRevive()
{
	const APawn* PawnOwner = Cast<APawn>(GetOwner());
	if (!PawnOwner || !PawnOwner->IsLocallyControlled())
	{
		return false;
	}
	APawn* Candidate = FindReviveCandidate();
	if (!Candidate)
	{
		return false;
	}
	ServerStartRevive(Candidate);
	return true;
}

void UBH_PlayerDeathComponent::StopRevive()
{
	const APawn* PawnOwner = Cast<APawn>(GetOwner());
	if (PawnOwner && PawnOwner->IsLocallyControlled())
	{
		ServerStopRevive();
	}
}

bool UBH_PlayerDeathComponent::ServerStartRevive_Validate(APawn* Target)
{
	return true;
}

void UBH_PlayerDeathComponent::ServerStartRevive_Implementation(APawn* Target)
{
	BeginReviveServer(Target);
}

void UBH_PlayerDeathComponent::ServerStopRevive_Implementation()
{
	CancelReviveAttempt();
}

bool UBH_PlayerDeathComponent::ValidateReviveTarget(const UBH_PlayerDeathComponent* TargetComp) const
{
	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !TargetComp || !TargetComp->CanBeRevivedBy(this))
	{
		return false;
	}
	if (FVector::Dist(OwnerActor->GetActorLocation(), TargetComp->GetBodyRestLocation()) > ReviveRange + ReviveRangeServerTolerance)
	{
		return false;
	}
	const UBH_PartyStateSubsystem* Party = UBH_PartyStateSubsystem::Get(this);
	return !Party || !Party->IsPartyInCombat();
}

bool UBH_PlayerDeathComponent::BeginReviveServer(APawn* Target)
{
	APawn* PawnOwner = Cast<APawn>(GetOwner());
	UBH_PlayerDeathComponent* TargetComp = Find(Target);
	if (!PawnOwner || !PawnOwner->HasAuthority() || !Target || Target == PawnOwner || !TargetComp)
	{
		return false;
	}
	if (!IsLivingPlayer(PawnOwner) || !ValidateReviveTarget(TargetComp))
	{
		return false;
	}

	CancelReviveAttempt();
	ReviveTargetPawn = Target;
	ReviveElapsed = 0.f;
	TargetComp->SetReviveInfo(PawnOwner, 0);

	// Interruptions: damage to the reviver (health change) and the party re-entering combat.
	if (bInterruptReviveOnDamage)
	{
		if (UAbilitySystemComponent* ASC = GetOwnerASC())
		{
			OwnerHealthHandle = ASC->GetGameplayAttributeValueChangeDelegate(UAH_AttributeSet::GetHealthAttribute())
				.AddUObject(this, &UBH_PlayerDeathComponent::HandleOwnerHealthChanged);
		}
	}
	if (UBH_PartyStateSubsystem* Party = UBH_PartyStateSubsystem::Get(this))
	{
		Party->OnPartyCombatChanged.AddDynamic(this, &UBH_PlayerDeathComponent::HandlePartyCombatChanged);
		bBoundToParty = true;
	}

	RefreshTickState();
	return true;
}

void UBH_PlayerDeathComponent::CancelReviveAttempt()
{
	UBH_PlayerDeathComponent* TargetComp = Find(ReviveTargetPawn.Get());
	ReviveTargetPawn.Reset();
	ReviveElapsed = 0.f;

	if (TargetComp && TargetComp->ReviveInfo.Reviver.Get() == GetOwner())
	{
		TargetComp->SetReviveInfo(nullptr, 0);
	}

	if (OwnerHealthHandle.IsValid())
	{
		if (UAbilitySystemComponent* ASC = GetOwnerASC())
		{
			ASC->GetGameplayAttributeValueChangeDelegate(UAH_AttributeSet::GetHealthAttribute()).Remove(OwnerHealthHandle);
		}
		OwnerHealthHandle.Reset();
	}
	if (bBoundToParty)
	{
		if (UBH_PartyStateSubsystem* Party = UBH_PartyStateSubsystem::Get(this))
		{
			Party->OnPartyCombatChanged.RemoveDynamic(this, &UBH_PlayerDeathComponent::HandlePartyCombatChanged);
		}
		bBoundToParty = false;
	}
	RefreshTickState();
}

void UBH_PlayerDeathComponent::TickRevive(float DeltaTime)
{
	APawn* PawnOwner = Cast<APawn>(GetOwner());
	APawn* Target = ReviveTargetPawn.Get();
	UBH_PlayerDeathComponent* TargetComp = Find(Target);
	if (!PawnOwner || !PawnOwner->GetController() || !IsLivingPlayer(PawnOwner) || !TargetComp || !ValidateReviveTarget(TargetComp))
	{
		CancelReviveAttempt(); // out of range, target gone / revived, reviver down, party back in combat, ...
		return;
	}

	ReviveElapsed += DeltaTime;
	const float Fraction = FMath::Clamp(ReviveElapsed / FMath::Max(ReviveHoldSeconds, 0.1f), 0.f, 1.f);
	if (Fraction >= 1.f)
	{
		TargetComp->ReviveInPlace(ReviveHealthPercent);
		CancelReviveAttempt();
		return;
	}
	const uint8 Percent = static_cast<uint8>(FMath::Min(100, FMath::FloorToInt(Fraction * 50.f) * 2)); // steps of 2: limits replication
	TargetComp->SetReviveInfo(PawnOwner, Percent);
}

void UBH_PlayerDeathComponent::HandleOwnerHealthChanged(const FOnAttributeChangeData& Data)
{
	if (Data.NewValue < Data.OldValue)
	{
		CancelReviveAttempt();
	}
}

void UBH_PlayerDeathComponent::HandlePartyCombatChanged(bool bInCombat)
{
	if (bInCombat)
	{
		CancelReviveAttempt();
	}
}

void UBH_PlayerDeathComponent::SetReviveInfo(APawn* Reviver, uint8 ProgressPct)
{
	if (ReviveInfo.Reviver == Reviver && ReviveInfo.ProgressPct == ProgressPct)
	{
		return;
	}
	ReviveInfo.Reviver = Reviver;
	ReviveInfo.ProgressPct = ProgressPct;
	if (AActor* OwnerActor = GetOwner())
	{
		OwnerActor->ForceNetUpdate();
	}
	BroadcastReviveProgress(); // the server / host gets no RepNotify
}

void UBH_PlayerDeathComponent::BroadcastReviveProgress()
{
	APawn* Reviver = ReviveInfo.Reviver;
	APawn* DeadPawn = Cast<APawn>(GetOwner());
	const float Fraction = Reviver ? GetReviveProgress() : 0.f;

	// The dead player's view ...
	OnReviveProgressChanged.Broadcast(Fraction, /*bIsDeadPlayerView*/ true, Reviver);

	// ... and the reviver's (a cleared reviver is still told that it stopped: OtherPlayer null).
	APawn* ReviverToNotify = Reviver ? Reviver : LastReportedReviver.Get();
	if (UBH_PlayerDeathComponent* ReviverComp = Find(ReviverToNotify))
	{
		ReviverComp->OnReviveProgressChanged.Broadcast(Fraction, /*bIsDeadPlayerView*/ false, Reviver ? DeadPawn : nullptr);
	}
	LastReportedReviver = Reviver;
}

// ============================================================================
// Debug (bh.Player.Kill / bh.Player.Revive)
// ============================================================================

#if !UE_BUILD_SHIPPING

bool UBH_PlayerDeathComponent::ServerDebugKill_Validate()
{
	return true;
}

void UBH_PlayerDeathComponent::ServerDebugKill_Implementation()
{
	UAbilitySystemComponent* ASC = GetOwnerASC();
	if (!ASC || DeathState.Phase != EBH_DeathPhase::Alive || ASC->HasMatchingGameplayTag(TAG_State_Combat_Dead))
	{
		UE_LOG(LogBHCombat, Warning, TEXT("bh.Player.Kill: %s is already down."), *GetNameSafe(GetOwner()));
		return;
	}

	// Lethal true damage through the normal pipeline (IncomingDamage -> Health -> OnHealthZero -> Dead tag).
	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddSourceObject(GetOwner());
	const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(UAH_GE_MeleeDamage::StaticClass(), 1.f, Context);
	if (Spec.IsValid() && Spec.Data.IsValid())
	{
		Spec.Data->SetSetByCallerMagnitude(TAG_Data_Damage, 1.0e6f);
		ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
	}

	if (!ASC->HasMatchingGameplayTag(TAG_State_Combat_Dead))
	{
		// Negated (parry window / i-frames): force the death.
		ASC->SetNumericAttributeBase(UAH_AttributeSet::GetHealthAttribute(), 0.f);
		ASC->AddLooseGameplayTag(TAG_State_Combat_Dead, 1, EGameplayTagReplicationState::TagOnly);
	}
	UE_LOG(LogBHCombat, Log, TEXT("bh.Player.Kill: %s killed."), *GetNameSafe(GetOwner()));
}

bool UBH_PlayerDeathComponent::ServerDebugRevive_Validate()
{
	return true;
}

void UBH_PlayerDeathComponent::ServerDebugRevive_Implementation()
{
	if (DeathState.Phase == EBH_DeathPhase::Alive)
	{
		UE_LOG(LogBHCombat, Warning, TEXT("bh.Player.Revive: %s is not down."), *GetNameSafe(GetOwner()));
		return;
	}
	ReviveInPlace(ReviveHealthPercent);
	UE_LOG(LogBHCombat, Log, TEXT("bh.Player.Revive: %s revived in place."), *GetNameSafe(GetOwner()));
}

#else // UE_BUILD_SHIPPING: stubs so the RPCs link; validation rejects every call.
bool UBH_PlayerDeathComponent::ServerDebugKill_Validate() { return false; }
void UBH_PlayerDeathComponent::ServerDebugKill_Implementation() {}
bool UBH_PlayerDeathComponent::ServerDebugRevive_Validate() { return false; }
void UBH_PlayerDeathComponent::ServerDebugRevive_Implementation() {}
#endif // !UE_BUILD_SHIPPING
