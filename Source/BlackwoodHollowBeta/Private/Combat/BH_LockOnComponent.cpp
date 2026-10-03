// Blackwood Hollow - Lock-on targeting component (implementation)

#include "Combat/BH_LockOnComponent.h"
#include "Characters/BH_EnemyBase.h"
#include "Combat/BH_CombatIdentityComponent.h"
#include "Combat/BH_StanceComponent.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystem/BH_CombatFunctionLibrary.h"
#include "UI/BH_HUDWidget.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "EnhancedInputComponent.h"
#include "EnhancedPlayerInput.h"
#include "InputAction.h"
#include "Components/CapsuleComponent.h"
#include "Components/DecalComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"
#include "UObject/UnrealType.h"

namespace BH_LockOn_Private
{
	static float AngleBetweenDeg(const FVector& A, const FVector& B)
	{
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(A.GetSafeNormal(), B.GetSafeNormal()), -1.f, 1.f)));
	}

	/** Lock-on candidates: the legacy ABH_EnemyBase, or any pawn carrying a UBH_CombatIdentityComponent. */
	static bool IsLockCandidate(const APawn* Pawn)
	{
		return Pawn && (Pawn->IsA<ABH_EnemyBase>() || UBH_CombatIdentityComponent::Find(Pawn) != nullptr);
	}
}

UBH_LockOnComponent::UBH_LockOnComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	SetIsReplicatedByDefault(true);
}

void UBH_LockOnComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UBH_LockOnComponent, LockedTarget, COND_OwnerOnly);
}

APlayerController* UBH_LockOnComponent::GetPC() const
{
	return Cast<APlayerController>(GetOwner());
}

bool UBH_LockOnComponent::IsLocalPC() const
{
	const APlayerController* PC = GetPC();
	return PC && PC->IsLocalController();
}

void UBH_LockOnComponent::BeginPlay()
{
	Super::BeginPlay();

	if (AActor* Owner = GetOwner())
	{
		AddTickPrerequisiteActor(Owner);
	}

	if (IsLocalPC())
	{
		TryBindInput();
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(SoftLockTimer, this, &UBH_LockOnComponent::SoftLockTimerTick, FMath::Max(SoftLockCacheInterval, 0.02f), true);
		}
	}
}

void UBH_LockOnComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(InputRetryHandle);
		World->GetTimerManager().ClearTimer(SoftLockTimer);
	}
	DestroySoftIndicator();
	bFacingOverrideActive = false;
	bRecoveringYaw = false;
	if (UWorld* OverrideWorld = GetWorld())
	{
		OverrideWorld->GetTimerManager().ClearTimer(OverrideTimeoutTimer);
	}
	if (bLocalLockActive)
	{
		DetachTargetCosmetics();
		ExitLocalLock();
	}
	Super::EndPlay(EndPlayReason);
}

// ============================================================================
// Input
// ============================================================================

void UBH_LockOnComponent::InitializeInput()
{
	if (IsLocalPC())
	{
		TryBindInput();
	}
}

void UBH_LockOnComponent::TryBindInput()
{
	if (bInputBound)
	{
		return;
	}

	APlayerController* PC = GetPC();
	UEnhancedInputComponent* EIC = PC ? Cast<UEnhancedInputComponent>(PC->InputComponent) : nullptr;
	if (!EIC)
	{
		// The controller's InputComponent is created after BeginPlay for local players; retry shortly.
		UWorld* World = GetWorld();
		if (World && InputBindRetries++ < 40)
		{
			World->GetTimerManager().SetTimer(InputRetryHandle, this, &UBH_LockOnComponent::TryBindInput, 0.25f, false);
		}
		return;
	}

	if (LockOnAction)
	{
		EIC->BindAction(LockOnAction, ETriggerEvent::Triggered, this, &UBH_LockOnComponent::OnLockOnInput);
	}
	if (SwitchTargetAction)
	{
		EIC->BindAction(SwitchTargetAction, ETriggerEvent::Triggered, this, &UBH_LockOnComponent::OnSwitchTargetInput);
	}
	bInputBound = true;
}

void UBH_LockOnComponent::OnLockOnInput(const FInputActionValue& Value)
{
	ToggleLockOn();
}

void UBH_LockOnComponent::OnSwitchTargetInput(const FInputActionValue& Value)
{
	SwitchTarget(Value.Get<float>());
}

// ============================================================================
// Public API
// ============================================================================

void UBH_LockOnComponent::ToggleLockOn()
{
	if (!IsLocalPC())
	{
		return;
	}

	if (LockedTarget)
	{
		ClearLockOn();
		return;
	}

	if (AActor* Best = FindBestTarget())
	{
		SetLockedTargetInternal(Best, true);
	}
}

void UBH_LockOnComponent::SwitchTarget(float Direction)
{
	if (!IsLocalPC() || !LockedTarget || FMath::IsNearlyZero(Direction))
	{
		return;
	}

	if (AActor* Next = FindSwitchTarget(Direction))
	{
		SetLockedTargetInternal(Next, true);
	}
}

void UBH_LockOnComponent::ClearLockOn()
{
	SetLockedTargetInternal(nullptr, true);
}

// ============================================================================
// Replication
// ============================================================================

void UBH_LockOnComponent::SetLockedTargetInternal(AActor* NewTarget, bool bNotifyServer)
{
	AActor* Old = LockedTarget;
	if (Old == NewTarget)
	{
		return;
	}

	LockedTarget = NewTarget;
	LineOfSightLostTime = 0.f;
	LockPawn = NewTarget && GetPC() ? GetPC()->GetPawn() : nullptr;

	SyncLocalState();

	if (bNotifyServer && GetOwner() && !GetOwner()->HasAuthority())
	{
		Server_SetLockedTarget(NewTarget);
	}

	OnLockedTargetChanged.Broadcast(Old, NewTarget);

	// Acquiring a lock-on draws the weapon (server side; the draw state replicates through the stance component).
	if (NewTarget && GetOwner() && GetOwner()->HasAuthority() && GetPC())
	{
		if (UBH_StanceComponent* StanceComp = UBH_StanceComponent::FindStanceComponent(GetPC()->GetPawn()))
		{
			StanceComp->NotifyCombatActivity();
		}
	}
}

void UBH_LockOnComponent::Server_SetLockedTarget_Implementation(AActor* NewTarget)
{
	UE_LOG(LogTemp, Log, TEXT("LockOn: Server_SetLockedTarget(%s) for %s"), *GetNameSafe(NewTarget), *GetNameSafe(GetOwner()));
	if (NewTarget && !IsTargetAcquirable(NewTarget))
	{
		Client_LockRejected(NewTarget);
		return;
	}
	SetLockedTargetInternal(NewTarget, false);
}

void UBH_LockOnComponent::Client_LockRejected_Implementation(AActor* RejectedTarget)
{
	UE_LOG(LogTemp, Log, TEXT("LockOn: Client_LockRejected(%s), local lock = %s"), *GetNameSafe(RejectedTarget), *GetNameSafe(LockedTarget));
	if (LockedTarget == RejectedTarget)
	{
		SetLockedTargetInternal(nullptr, false);
	}
}

void UBH_LockOnComponent::OnRep_LockedTarget(AActor* OldTarget)
{
	LineOfSightLostTime = 0.f;
	LockPawn = LockedTarget && GetPC() ? GetPC()->GetPawn() : nullptr;
	SyncLocalState();
	OnLockedTargetChanged.Broadcast(OldTarget, LockedTarget);
}

// ============================================================================
// Target rules
// ============================================================================

bool UBH_LockOnComponent::IsTargetDead(const AActor* Target) const
{
	const UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(const_cast<AActor*>(Target));
	if (!ASC)
	{
		return true;
	}
	if (ASC->HasMatchingGameplayTag(TAG_State_Combat_Dead))
	{
		return true;
	}
	// The Dead tag is a loose tag and may not reach clients; Health does.
	if (ASC->HasAttributeSetForAttribute(UAH_AttributeSet::GetHealthAttribute()))
	{
		return ASC->GetNumericAttribute(UAH_AttributeSet::GetHealthAttribute()) <= 0.f;
	}
	return false;
}

bool UBH_LockOnComponent::IsTargetAcquirable(const AActor* Target) const
{
	const APlayerController* PC = GetPC();
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn || !IsValid(Target) || Target == Pawn)
	{
		return false;
	}
	if (UBH_CombatFunctionLibrary::AreCombatAllies(Pawn, Target))
	{
		return false;
	}
	if (IsTargetDead(Target))
	{
		return false;
	}
	return FVector::Dist(Pawn->GetActorLocation(), Target->GetActorLocation()) <= BreakRange;
}

float UBH_LockOnComponent::GetTargetHalfHeight(const AActor* Target) const
{
	if (const ACharacter* Character = Cast<ACharacter>(Target))
	{
		return Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	}
	FVector Origin, Extent;
	Target->GetActorBounds(true, Origin, Extent);
	return Extent.Z;
}

FVector UBH_LockOnComponent::GetAimPoint(const AActor* Target) const
{
	return Target->GetActorLocation() + FVector(0.f, 0.f, GetTargetHalfHeight(Target) * (2.f * TargetHeightFraction - 1.f));
}

FVector UBH_LockOnComponent::GetViewOrigin() const
{
	const APlayerController* PC = GetPC();
	if (PC && PC->IsLocalController() && PC->PlayerCameraManager)
	{
		return PC->PlayerCameraManager->GetCameraLocation();
	}
	if (const APawn* Pawn = PC ? PC->GetPawn() : nullptr)
	{
		return Pawn->GetPawnViewLocation();
	}
	return FVector::ZeroVector;
}

bool UBH_LockOnComponent::HasLineOfSight(const AActor* Target) const
{
	const APlayerController* PC = GetPC();
	UWorld* World = GetWorld();
	if (!PC || !World)
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BH_LockOnLOS), false);
	Params.AddIgnoredActor(PC->GetPawn());
	Params.AddIgnoredActor(Target);
	FHitResult Hit;
	return !World->LineTraceSingleByChannel(Hit, GetViewOrigin(), GetAimPoint(Target), ECC_Visibility, Params);
}

AActor* UBH_LockOnComponent::FindBestTarget() const
{
	using namespace BH_LockOn_Private;

	const APlayerController* PC = GetPC();
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	UWorld* World = GetWorld();
	if (!Pawn || !World || !PC->PlayerCameraManager)
	{
		return nullptr;
	}

	const FVector CamLoc = PC->PlayerCameraManager->GetCameraLocation();
	const FVector CamFwd = PC->PlayerCameraManager->GetCameraRotation().Vector();

	AActor* Best = nullptr;
	float BestScore = TNumericLimits<float>::Max();
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		APawn* Candidate = *It;
		if (!IsLockCandidate(Candidate) || !IsTargetAcquirable(Candidate))
		{
			continue;
		}
		const float Dist = FVector::Dist(Pawn->GetActorLocation(), Candidate->GetActorLocation());
		if (Dist > AcquireRange)
		{
			continue;
		}
		const float Angle = AngleBetweenDeg(CamFwd, GetAimPoint(Candidate) - CamLoc);
		if (Angle > AcquireHalfAngleDeg)
		{
			continue;
		}
		if (!HasLineOfSight(Candidate))
		{
			continue;
		}
		const float Score = 0.65f * (Angle / AcquireHalfAngleDeg) + 0.35f * (Dist / AcquireRange);
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = Candidate;
		}
	}
	return Best;
}

AActor* UBH_LockOnComponent::FindSwitchTarget(float Direction) const
{
	using namespace BH_LockOn_Private;

	APlayerController* PC = GetPC();
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	UWorld* World = GetWorld();
	if (!Pawn || !World || !LockedTarget)
	{
		return nullptr;
	}

	FVector2D CurrentScreen;
	if (!PC->ProjectWorldLocationToScreen(GetAimPoint(LockedTarget), CurrentScreen, false))
	{
		return nullptr;
	}

	const float Sign = Direction > 0.f ? 1.f : -1.f;
	AActor* Best = nullptr;
	float BestDelta = TNumericLimits<float>::Max();
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		APawn* Candidate = *It;
		if (!IsLockCandidate(Candidate) || Candidate == LockedTarget || !IsTargetAcquirable(Candidate))
		{
			continue;
		}
		if (FVector::Dist(Pawn->GetActorLocation(), Candidate->GetActorLocation()) > AcquireRange)
		{
			continue;
		}
		FVector2D Screen;
		if (!PC->ProjectWorldLocationToScreen(GetAimPoint(Candidate), Screen, false))
		{
			continue;
		}
		const float Delta = (Screen.X - CurrentScreen.X) * Sign;
		if (Delta <= 0.f || Delta >= BestDelta)
		{
			continue;
		}
		if (!HasLineOfSight(Candidate))
		{
			continue;
		}
		BestDelta = Delta;
		Best = Candidate;
	}
	return Best;
}

// Runs on the owning client and on the server. Returns false when the lock was broken.
bool UBH_LockOnComponent::ValidateLock(float DeltaTime)
{
	const APlayerController* PC = GetPC();
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;

	bool bBreak = false;
	if (!IsValid(LockedTarget) || !Pawn || Pawn != LockPawn.Get())
	{
		bBreak = true;
	}
	else if (IsTargetDead(LockedTarget))
	{
		bBreak = true;
	}
	else if (FVector::Dist(Pawn->GetActorLocation(), LockedTarget->GetActorLocation()) > BreakRange)
	{
		bBreak = true;
	}
	else if (HasLineOfSight(LockedTarget))
	{
		LineOfSightLostTime = 0.f;
	}
	else
	{
		LineOfSightLostTime += DeltaTime;
		bBreak = LineOfSightLostTime > LoseLineOfSightGrace;
	}

	if (bBreak)
	{
		AActor* Broken = LockedTarget;
		SetLockedTargetInternal(nullptr, true);
		// The owning client keeps a predicted lock whose value the server never replicated; tell it explicitly.
		if (!IsLocalPC() && GetOwner() && GetOwner()->HasAuthority())
		{
			Client_LockRejected(Broken);
		}
		return false;
	}
	return true;
}

// ============================================================================
// Tick
// ============================================================================

void UBH_LockOnComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bFacingOverrideActive || bRecoveringYaw)
	{
		UpdateFacingHold(DeltaTime);
	}

	if (!LockedTarget)
	{
		return;
	}

	const bool bLocal = IsLocalPC();
	const bool bAuthority = GetOwner() && GetOwner()->HasAuthority();
	if (!bLocal && !bAuthority)
	{
		return;
	}

	if (!ValidateLock(DeltaTime))
	{
		return;
	}

	if (bLocal)
	{
		PollFlick(DeltaTime);
		if (LockedTarget)
		{
			UpdateCamera(DeltaTime);
		}

		// Ring fade-in.
		if (RingMID && RingFade < 1.f)
		{
			RingFade = GroundRingFadeTime > 0.f ? FMath::Min(1.f, RingFade + DeltaTime / GroundRingFadeTime) : 1.f;
			RingMID->SetScalarParameterValue(TEXT("Opacity"), RingFade);
		}
	}
}

void UBH_LockOnComponent::UpdateCamera(float DeltaTime)
{
	APlayerController* PC = GetPC();
	if (!PC || !PC->PlayerCameraManager || !LockedTarget)
	{
		return;
	}

	const FVector CamLoc = PC->PlayerCameraManager->GetCameraLocation();
	FRotator Desired = (GetAimPoint(LockedTarget) - CamLoc).Rotation();
	Desired.Pitch = FMath::Clamp(FRotator::NormalizeAxis(Desired.Pitch) + PitchBias, PitchMin, PitchMax);

	const FRotator Current = PC->GetControlRotation();
	const float YawDelta = FMath::FindDeltaAngleDegrees(Current.Yaw, Desired.Yaw);
	const float PitchDelta = FMath::FindDeltaAngleDegrees(Current.Pitch, Desired.Pitch);

	FRotator NewRot = Current;
	NewRot.Yaw = FRotator::NormalizeAxis(Current.Yaw + FMath::FInterpTo(0.f, YawDelta, DeltaTime, CameraYawInterpSpeed));
	NewRot.Pitch = FRotator::NormalizeAxis(Current.Pitch + FMath::FInterpTo(0.f, PitchDelta, DeltaTime, CameraPitchInterpSpeed));
	NewRot.Roll = 0.f;
	PC->SetControlRotation(NewRot);
}

void UBH_LockOnComponent::PollFlick(float DeltaTime)
{
	FlickTimer = FMath::Max(0.f, FlickTimer - DeltaTime);

	const APlayerController* PC = GetPC();
	const UEnhancedPlayerInput* Input = PC ? Cast<UEnhancedPlayerInput>(PC->PlayerInput) : nullptr;
	if (!Input)
	{
		return;
	}

	float FlickDir = 0.f;

	if (LookGamepadAction)
	{
		const float X = Input->GetActionValue(LookGamepadAction).Get<FVector2D>().X;
		const bool bOver = FMath::Abs(X) >= FlickThreshold;
		if (bOver && !bGamepadFlickHeld)
		{
			FlickDir = X;
		}
		bGamepadFlickHeld = bOver;
	}

	if (LookAction)
	{
		const float X = Input->GetActionValue(LookAction).Get<FVector2D>().X;
		const bool bOver = FMath::Abs(X) >= MouseFlickThreshold;
		if (bOver && !bMouseFlickHeld && FlickDir == 0.f)
		{
			FlickDir = X;
		}
		bMouseFlickHeld = bOver;
	}

	if (FlickDir != 0.f && FlickTimer <= 0.f)
	{
		FlickTimer = FlickCooldown;
		SwitchTarget(FlickDir);
	}
}

// ============================================================================
// Local state (camera lock, outline, ring, strafe)
// ============================================================================

void UBH_LockOnComponent::SyncLocalState()
{
	if (!IsLocalPC())
	{
		return;
	}

	if (LockedTarget)
	{
		// Hard lock supersedes the soft marker.
		SoftTarget = nullptr;
		DestroySoftIndicator();
	}

	AActor* Applied = AppliedTarget.Get();
	AActor* Wanted = LockedTarget;
	if (Applied == Wanted && (Wanted != nullptr) == bLocalLockActive)
	{
		return;
	}

	DetachTargetCosmetics();

	if (Wanted)
	{
		if (!bLocalLockActive)
		{
			EnterLocalLock();
		}
		AttachTargetCosmetics(Wanted);
	}
	else if (bLocalLockActive)
	{
		ExitLocalLock();
	}

	AppliedTarget = Wanted;

	if (APlayerController* PC = GetPC())
	{
		UBH_HUDWidget::BroadcastLockedTargetChanged(PC, Wanted);
	}
}

void UBH_LockOnComponent::EnterLocalLock()
{
	APlayerController* PC = GetPC();
	if (!PC)
	{
		return;
	}
	bLocalLockActive = true;

	if (!bIgnoringLook)
	{
		PC->SetIgnoreLookInput(true);
		bIgnoringLook = true;
	}

	ForceStrafe();

	bGamepadFlickHeld = true; // a held stick / mouse motion at lock time must not trigger an immediate switch
	bMouseFlickHeld = true;
	FlickTimer = FlickCooldown;

	// The post process component lives on the pawn, not the controller: controllers are hidden actors and a
	// component whose owner is hidden never contributes post process settings.
	if (OutlinePostProcessMaterial)
	{
		APawn* Pawn = PC->GetPawn();
		if (OutlinePPComp && OutlinePPComp->GetOwner() != Pawn)
		{
			OutlinePPComp->DestroyComponent();
			OutlinePPComp = nullptr;
		}
		if (!OutlinePPComp && Pawn)
		{
			OutlinePPComp = NewObject<UPostProcessComponent>(Pawn, TEXT("LockOnOutlinePP"));
			OutlinePPComp->bUnbound = true;
			OutlinePPComp->Priority = 10.f;
			OutlinePPComp->BlendWeight = 1.f;
			OutlinePPComp->AddOrUpdateBlendable(OutlinePostProcessMaterial, 1.f);
			if (Pawn->GetRootComponent())
			{
				OutlinePPComp->SetupAttachment(Pawn->GetRootComponent());
			}
			OutlinePPComp->RegisterComponent();
		}
		if (OutlinePPComp)
		{
			OutlinePPComp->bEnabled = true;
		}
	}
}

void UBH_LockOnComponent::ExitLocalLock()
{
	bLocalLockActive = false;

	if (APlayerController* PC = GetPC())
	{
		if (bIgnoringLook)
		{
			PC->SetIgnoreLookInput(false);
		}
	}
	bIgnoringLook = false;

	RestoreStrafe();

	if (OutlinePPComp)
	{
		OutlinePPComp->bEnabled = false;
	}
}

void UBH_LockOnComponent::AttachTargetCosmetics(AActor* Target)
{
	if (!Target)
	{
		return;
	}

	// Outline: every primitive on the target (skeletal mesh + attached weapons) writes the stencil.
	TArray<UPrimitiveComponent*> Prims;
	Target->GetComponents<UPrimitiveComponent>(Prims);
	for (UPrimitiveComponent* Prim : Prims)
	{
		if (!Prim || Prim->IsA<UCapsuleComponent>() || Prim->IsA<UDecalComponent>())
		{
			continue;
		}
		FOutlinedComp Entry;
		Entry.Comp = Prim;
		Entry.bPrevRenderCustomDepth = Prim->bRenderCustomDepth;
		Entry.PrevStencil = Prim->CustomDepthStencilValue;
		Entry.bPrevReceivesDecals = Prim->bReceivesDecals;
		OutlinedComps.Add(Entry);

		Prim->SetCustomDepthStencilValue(TargetStencilValue);
		Prim->SetRenderCustomDepth(true);
		// The ring decal is meant for the floor only; keep it off the target's own legs.
		Prim->SetReceivesDecals(false);
	}

	// Ground ring.
	if (GroundRingMaterial && Target->GetRootComponent())
	{
		RingMID = UMaterialInstanceDynamic::Create(GroundRingMaterial, this);
		RingFade = 0.f;
		RingMID->SetScalarParameterValue(TEXT("Opacity"), 0.f);

		RingDecal = NewObject<UDecalComponent>(Target, TEXT("LockOnRingDecal"));
		RingDecal->SetDecalMaterial(RingMID);
		RingDecal->DecalSize = GroundRingDecalSize;
		RingDecal->SetupAttachment(Target->GetRootComponent());
		RingDecal->SetRelativeLocation(FVector(0.f, 0.f, -GetTargetHalfHeight(Target) + 2.f));
		RingDecal->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f));
		RingDecal->SetIsReplicated(false);
		RingDecal->RegisterComponent();
	}
}

void UBH_LockOnComponent::DetachTargetCosmetics()
{
	for (const FOutlinedComp& Entry : OutlinedComps)
	{
		if (UPrimitiveComponent* Prim = Entry.Comp.Get())
		{
			Prim->SetRenderCustomDepth(Entry.bPrevRenderCustomDepth);
			Prim->SetCustomDepthStencilValue(Entry.PrevStencil);
			Prim->SetReceivesDecals(Entry.bPrevReceivesDecals);
		}
	}
	OutlinedComps.Reset();

	if (RingDecal)
	{
		RingDecal->DestroyComponent();
		RingDecal = nullptr;
	}
	RingMID = nullptr;
}


// ============================================================================
// Soft lock (attack magnetism): no camera control, no strafe forcing
// ============================================================================

AActor* UBH_LockOnComponent::GetSoftTarget() const
{
	AActor* Target = SoftTarget.Get();
	return (!LockedTarget && IsValid(Target)) ? Target : nullptr;
}

AActor* UBH_LockOnComponent::RefreshSoftTarget()
{
	AActor* Found = LockedTarget ? nullptr : FindSoftTarget();
	SoftTarget = Found;
	if (const UWorld* World = GetWorld())
	{
		SoftTargetTime = World->GetTimeSeconds();
	}
	return Found;
}

bool UBH_LockOnComponent::FaceMeleeTarget(bool bAllowHard, bool bAllowSoft, bool bNotifyServer, bool& bOutUsedSoftTarget)
{
	bOutUsedSoftTarget = false;
	APlayerController* PC = GetPC();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn)
	{
		return false;
	}

	AActor* Target = nullptr;
	bool bHard = false;
	if (LockedTarget && bAllowHard)
	{
		Target = LockedTarget;
		bHard = true;
	}
	else if (!LockedTarget && bAllowSoft)
	{
		Target = RefreshSoftTarget();
	}
	if (!Target)
	{
		return false;
	}

	const FVector ToTarget = (Target->GetActorLocation() - Pawn->GetActorLocation()).GetSafeNormal2D();
	if (ToTarget.IsNearlyZero())
	{
		return false;
	}

	FRotator NewRot = Pawn->GetActorRotation();
	const float Delta = FMath::FindDeltaAngleDegrees(NewRot.Yaw, ToTarget.Rotation().Yaw);
	if (!bHard && FMath::Abs(Delta) > MaxSoftLockTurnDeg)
	{
		return false; // never spin the player around to someone behind them
	}
	NewRot.Yaw = ToTarget.Rotation().Yaw;
	Pawn->SetActorRotation(NewRot);
	if (!bHard)
	{
		bOutUsedSoftTarget = true;
		BeginMeleeFacingOverride(NewRot.Yaw);
	}

	if (bNotifyServer && IsLocalPC() && GetOwner() && !GetOwner()->HasAuthority())
	{
		Server_ApplyMeleeFacing(Target);
	}
	return true;
}

void UBH_LockOnComponent::Server_ApplyMeleeFacing_Implementation(AActor* Target)
{
	APlayerController* PC = GetPC();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn || !IsValid(Target) || !IsTargetAcquirable(Target))
	{
		return;
	}
	// Soft targets must be inside the soft range (with slack for client/server position drift); the hard target always passes.
	if (Target != LockedTarget && FVector::Dist(Pawn->GetActorLocation(), Target->GetActorLocation()) > SoftLockRange * 1.25f)
	{
		return;
	}
	const FVector ToTarget = (Target->GetActorLocation() - Pawn->GetActorLocation()).GetSafeNormal2D();
	if (!ToTarget.IsNearlyZero())
	{
		FRotator NewRot = Pawn->GetActorRotation();
		NewRot.Yaw = ToTarget.Rotation().Yaw;
		Pawn->SetActorRotation(NewRot);
		if (Target != LockedTarget)
		{
			BeginMeleeFacingOverride(NewRot.Yaw);
		}
	}
}

void UBH_LockOnComponent::BeginMeleeFacingOverride(float Yaw)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	HeldYaw = Yaw;
	bFacingOverrideActive = true;
	bRecoveringYaw = false;
	World->GetTimerManager().SetTimer(OverrideTimeoutTimer, this, &UBH_LockOnComponent::EndMeleeFacingOverride, SoftLockMaxOverrideTime, false);
}

void UBH_LockOnComponent::EndMeleeFacingOverride()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(OverrideTimeoutTimer);
	}
	if (bFacingOverrideActive)
	{
		bFacingOverrideActive = false;
		bRecoveringYaw = SoftLockRecoverTurnRate > 0.f; // ease back to the camera yaw instead of popping
	}
}

void UBH_LockOnComponent::UpdateFacingHold(float DeltaTime)
{
	const APlayerController* PC = GetPC();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn)
	{
		bFacingOverrideActive = false;
		bRecoveringYaw = false;
		return;
	}

	if (bRecoveringYaw)
	{
		const float CameraYaw = PC->GetControlRotation().Yaw;
		HeldYaw = FMath::FixedTurn(HeldYaw, CameraYaw, SoftLockRecoverTurnRate * DeltaTime);
		if (FMath::IsNearlyEqual(FMath::FindDeltaAngleDegrees(HeldYaw, CameraYaw), 0.f, 0.5f))
		{
			bRecoveringYaw = false;
		}
	}

	FRotator Rot = Pawn->GetActorRotation();
	Rot.Yaw = HeldYaw;
	Pawn->SetActorRotation(Rot);
}

AActor* UBH_LockOnComponent::FindSoftTarget() const
{
	using namespace BH_LockOn_Private;

	const APlayerController* PC = GetPC();
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	UWorld* World = GetWorld();
	if (!Pawn || !World || LockedTarget)
	{
		return nullptr;
	}

	// Reference direction: movement input (CMC acceleration is valid on the server for a remote client's moves),
	// else camera forward (control yaw is replicated to the server).
	FVector Ref = FVector::ZeroVector;
	if (const ACharacter* Character = Cast<ACharacter>(Pawn))
	{
		if (const UCharacterMovementComponent* Move = Character->GetCharacterMovement())
		{
			Ref = Move->GetCurrentAcceleration();
		}
	}
	if (Ref.SizeSquared2D() < 1.f)
	{
		Ref = Pawn->GetLastMovementInputVector() * 100.f;
	}
	Ref.Z = 0.f;
	if (Ref.SizeSquared2D() < 1.f)
	{
		Ref = FRotator(0.f, PC->GetControlRotation().Yaw, 0.f).Vector();
	}
	Ref = Ref.GetSafeNormal2D();

	const FVector Origin = Pawn->GetActorLocation();
	AActor* Best = nullptr;
	float BestScore = TNumericLimits<float>::Max();
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		APawn* Candidate = *It;
		if (!IsLockCandidate(Candidate) || !IsTargetAcquirable(Candidate))
		{
			continue;
		}
		const FVector ToCand = Candidate->GetActorLocation() - Origin;
		const float Dist = ToCand.Size();
		if (Dist > SoftLockRange)
		{
			continue;
		}
		const FVector Dir2D = ToCand.GetSafeNormal2D();
		const float Angle = Dir2D.IsNearlyZero() ? 0.f : AngleBetweenDeg(Ref, Dir2D);
		if (Angle > SoftLockHalfAngleDeg)
		{
			continue;
		}
		FCollisionQueryParams Params(SCENE_QUERY_STAT(BH_SoftLockLOS), false);
		Params.AddIgnoredActor(Pawn);
		Params.AddIgnoredActor(Candidate);
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, Origin, GetAimPoint(Candidate), ECC_Visibility, Params))
		{
			continue;
		}
		const float Score = 0.5f * (Angle / SoftLockHalfAngleDeg) + 0.5f * (Dist / SoftLockRange);
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = Candidate;
		}
	}
	return Best;
}

void UBH_LockOnComponent::SoftLockTimerTick()
{
	if (!IsLocalPC())
	{
		return;
	}
	if (LockedTarget)
	{
		SoftTarget = nullptr;
		DestroySoftIndicator();
		return;
	}
	AActor* Target = RefreshSoftTarget();
	UpdateSoftIndicator(bShowSoftLockIndicator ? Target : nullptr);
}

void UBH_LockOnComponent::UpdateSoftIndicator(AActor* Target)
{
	if (!Target || !GroundRingMaterial || !Target->GetRootComponent())
	{
		DestroySoftIndicator();
		return;
	}
	if (SoftIndicatorTarget.Get() == Target && SoftDecal)
	{
		SoftMID->SetScalarParameterValue(TEXT("Opacity"), SoftLockIndicatorOpacity);
		return;
	}

	DestroySoftIndicator();

	SoftMID = UMaterialInstanceDynamic::Create(GroundRingMaterial, this);
	SoftMID->SetScalarParameterValue(TEXT("Opacity"), SoftLockIndicatorOpacity);

	SoftDecal = NewObject<UDecalComponent>(Target, TEXT("SoftLockDecal"));
	SoftDecal->SetDecalMaterial(SoftMID);
	SoftDecal->DecalSize = FVector(GroundRingDecalSize.X, GroundRingDecalSize.Y * SoftLockIndicatorScale, GroundRingDecalSize.Z * SoftLockIndicatorScale);
	SoftDecal->SetupAttachment(Target->GetRootComponent());
	SoftDecal->SetRelativeLocation(FVector(0.f, 0.f, -GetTargetHalfHeight(Target) + 2.f));
	SoftDecal->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f));
	SoftDecal->SetIsReplicated(false);
	SoftDecal->RegisterComponent();
	SoftIndicatorTarget = Target;
}

void UBH_LockOnComponent::DestroySoftIndicator()
{
	if (SoftDecal)
	{
		SoftDecal->DestroyComponent();
		SoftDecal = nullptr;
	}
	SoftMID = nullptr;
	SoftIndicatorTarget = nullptr;
}

// ============================================================================
// GASP strafe (reflection lives in UBH_CombatFunctionLibrary::SetCharacterWantsToStrafe)
// ============================================================================

void UBH_LockOnComponent::ForceStrafe()
{
	APawn* Pawn = GetPC() ? GetPC()->GetPawn() : nullptr;
	bool bPrevious = true;
	if (UBH_CombatFunctionLibrary::SetCharacterWantsToStrafe(Pawn, true, &bPrevious))
	{
		StrafePawn = Pawn;
		bStrafeWasChanged = !bPrevious;
	}
}

void UBH_LockOnComponent::RestoreStrafe()
{
	if (bStrafeWasChanged)
	{
		if (APawn* Pawn = StrafePawn.Get())
		{
			UBH_CombatFunctionLibrary::SetCharacterWantsToStrafe(Pawn, false, nullptr);
		}
	}
	bStrafeWasChanged = false;
	StrafePawn = nullptr;
}
