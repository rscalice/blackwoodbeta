// Blackwood Hollow - native base for the motion-matching character (implementation)

#include "Characters/BH_CharacterBase.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystem/BH_CombatFunctionLibrary.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "Combat/BH_StanceComponent.h"
#include "Player/BH_PlayerDeathComponent.h"
#include "Interaction/BH_InteractorComponent.h"
#include "Items/BH_ArmorVisualComponent.h"
#include "Combat/BH_CombatFeel.h"
#include "Consumables/BH_GA_UseConsumable.h"
#include "Characters/BH_StanceMovementProfile.h"
#include "AbilitySystemComponent.h"
#include "AIController.h"
#include "EnhancedInputComponent.h"
#include "InputAction.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Net/UnrealNetwork.h"

namespace BH_CharacterBase_Private
{
	static TAutoConsoleVariable<int32> CVarRagdollDebugCamera(
		TEXT("bh.Ragdoll.DebugCamera"),
		0,
		TEXT("1 = when a ragdoll starts, log every component of the pawn and of its attached actors that still responds to the Camera channel (Phase 11A-1 diagnostics)."),
		ECVF_Default);
}

ABH_CharacterBase::ABH_CharacterBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	AttributeSet = CreateDefaultSubobject<UAH_AttributeSet>(TEXT("AttributeSet"));
	StanceComponent = CreateDefaultSubobject<UBH_StanceComponent>(TEXT("StanceComponent"));
	DeathComponent = CreateDefaultSubobject<UBH_PlayerDeathComponent>(TEXT("DeathComponent"));
	InteractorComponent = CreateDefaultSubobject<UBH_InteractorComponent>(TEXT("InteractorComponent"));
	ArmorVisualComponent = CreateDefaultSubobject<UBH_ArmorVisualComponent>(TEXT("ArmorVisualComponent"));

	// Attack telegraph decals must not tint the character itself.
	if (USkeletalMeshComponent* SkelMesh = GetMesh())
	{
		SkelMesh->SetReceivesDecals(false);
	}
}

void ABH_CharacterBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABH_CharacterBase, CombatTeam);
	DOREPLIFETIME(ABH_CharacterBase, bLockOnStrafe);
	DOREPLIFETIME(ABH_CharacterBase, AIDesiredGait);
}

void ABH_CharacterBase::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	if (AbilitySystemComponent && Cast<AAIController>(NewController))
	{
		AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);
	}
	InitAbilitySystem();

	if (CombatTeam != EBH_CombatTeam::Neutral)
	{
		if (IGenericTeamAgentInterface* ControllerAgent = Cast<IGenericTeamAgentInterface>(NewController))
		{
			ControllerAgent->SetGenericTeamId(GetGenericTeamId());
		}
	}
}

void ABH_CharacterBase::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	InitAbilitySystem();
}

void ABH_CharacterBase::BeginPlay()
{
	Super::BeginPlay();

	InitAbilitySystem();
	if (HasAuthority())
	{
		GrantDefaultAbilities();
		UBH_CombatFunctionLibrary::ApplyPassiveRegenEffects(this);
	}
}

void ABH_CharacterBase::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// IA_Interact (ReviveInputAction): hold-to-revive (death component) and interact / hold-to-harvest (Phase 11C interactor) share the one
	// action; bound here (local player only, every possession) so the Blueprint needs no input nodes. Each handler ignores the press when the
	// other one owns it (a downed party member in range = revive, else the focused interactable).
	if (!ReviveInputAction)
	{
		return;
	}
	if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (DeathComponent)
		{
			EnhancedInput->BindAction(ReviveInputAction, ETriggerEvent::Started, DeathComponent.Get(), &UBH_PlayerDeathComponent::OnReviveInputPressed);
			EnhancedInput->BindAction(ReviveInputAction, ETriggerEvent::Completed, DeathComponent.Get(), &UBH_PlayerDeathComponent::OnReviveInputReleased);
			EnhancedInput->BindAction(ReviveInputAction, ETriggerEvent::Canceled, DeathComponent.Get(), &UBH_PlayerDeathComponent::OnReviveInputReleased);
		}
		if (InteractorComponent)
		{
			EnhancedInput->BindAction(ReviveInputAction, ETriggerEvent::Started, InteractorComponent.Get(), &UBH_InteractorComponent::OnInteractInputPressed);
			EnhancedInput->BindAction(ReviveInputAction, ETriggerEvent::Completed, InteractorComponent.Get(), &UBH_InteractorComponent::OnInteractInputReleased);
			EnhancedInput->BindAction(ReviveInputAction, ETriggerEvent::Canceled, InteractorComponent.Get(), &UBH_InteractorComponent::OnInteractInputReleased);
		}
	}
}

void ABH_CharacterBase::InitAbilitySystem()
{
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->InitAbilityActorInfo(this, this);
	}
}

void ABH_CharacterBase::GrantDefaultAbilities()
{
	if (bAbilitiesGranted)
	{
		return;
	}
	UBH_CombatFunctionLibrary::GrantCombatAbilities(this, DefaultAbilities);

	// Phase 11D: every pawn can use consumables. The native ability needs no Blueprint (skipped when a child is already in DefaultAbilities).
	if (AbilitySystemComponent && !AbilitySystemComponent->FindAbilitySpecFromClass(UBH_GA_UseConsumable::StaticClass()))
	{
		AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec(UBH_GA_UseConsumable::StaticClass(), 1, INDEX_NONE, this));
	}
	bAbilitiesGranted = true;
}

void ABH_CharacterBase::SetLockOnStrafe(bool bEnabled)
{
	if (bLockOnStrafe == bEnabled)
	{
		return;
	}
	bLockOnStrafe = bEnabled;
	if (!HasAuthority())
	{
		ServerSetLockOnStrafe(bEnabled);
	}
}

void ABH_CharacterBase::ServerSetLockOnStrafe_Implementation(bool bEnabled)
{
	bLockOnStrafe = bEnabled;
}

void ABH_CharacterBase::OnRep_LockOnStrafe()
{
}

void ABH_CharacterBase::Client_ApplyHitPushback_Implementation(FVector Direction, float Distance, float Duration, uint16 Id)
{
	// Autonomous proxy only (the server never RPCs itself): same source as the server applied, so the next move agrees.
	if (!HasAuthority())
	{
		UBH_CombatFeelLibrary::ApplyPushbackSource(this, Direction, Distance, Duration, Id);
	}
}

EBH_RotationMode ABH_CharacterBase::GetRotationMode() const
{
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	return Movement && Movement->bUseControllerDesiredRotation ? EBH_RotationMode::Strafe : EBH_RotationMode::OrientToMovement;
}

void ABH_CharacterBase::SetGaitFromByte(uint8 GaitByte)
{
	CurrentGait = static_cast<EBH_Gait>(FMath::Clamp<int32>(GaitByte, 0, 2));
}

FGameplayTag ABH_CharacterBase::GetWeaponStance() const
{
	return StanceComponent ? StanceComponent->GetCurrentStance() : FGameplayTag();
}

UBH_StanceMovementProfile* ABH_CharacterBase::GetMovementProfile() const
{
	return StanceComponent ? StanceComponent->GetActiveMovementProfile() : nullptr;
}

void ABH_CharacterBase::SetAIDesiredGait(EBH_Gait NewGait)
{
	if (HasAuthority())
	{
		AIDesiredGait = NewGait;
	}
}

bool ABH_CharacterBase::IsCombatMovementLocked() const
{
	// Down (ragdoll / waiting for revive) or getting up: no movement, whatever the tags say.
	if (DeathComponent && DeathComponent->IsMovementBlocked())
	{
		return true;
	}
	return AbilitySystemComponent
		&& (AbilitySystemComponent->HasMatchingGameplayTag(TAG_State_Combat_MovementLocked)
			|| AbilitySystemComponent->HasMatchingGameplayTag(TAG_State_Combat_Dead));
}

void ABH_CharacterBase::AddMovementInput(FVector WorldDirection, float ScaleValue, bool bForce)
{
	if (!bForce && IsCombatMovementLocked())
	{
		return;
	}
	Super::AddMovementInput(WorldDirection, ScaleValue, bForce);
}

FVector ABH_CharacterBase::GetGaitSpeedsOr(FVector Fallback) const
{
	if (IsCombatMovementLocked())
	{
		return FVector::ZeroVector;
	}
	const UBH_StanceMovementProfile* Profile = GetMovementProfile();
	return Profile ? Profile->GetGaitSettings(CurrentGait).Speeds : Fallback;
}

FVector ABH_CharacterBase::GetCrouchSpeedsOr(FVector Fallback) const
{
	const UBH_StanceMovementProfile* Profile = GetMovementProfile();
	return Profile ? Profile->CrouchSpeeds : Fallback;
}

float ABH_CharacterBase::GetMaxAccelerationOr(float Fallback) const
{
	if (IsCombatMovementLocked())
	{
		return 0.f;
	}
	const UBH_StanceMovementProfile* Profile = GetMovementProfile();
	return Profile ? Profile->GetMaxAccelerationFor(CurrentGait, GetVelocity().Size2D()) : Fallback;
}

float ABH_CharacterBase::GetGroundFrictionOr(float Fallback) const
{
	const UBH_StanceMovementProfile* Profile = GetMovementProfile();
	return Profile ? Profile->GetGroundFrictionFor(CurrentGait, GetVelocity().Size2D()) : Fallback;
}

float ABH_CharacterBase::ComputeBrakingDecelerationOr(bool bHasMovementInput, float Fallback)
{
	if (IsCombatMovementLocked())
	{
		BrakingBand.Reset();
		return CombatLockBrakingDeceleration;
	}
	const UBH_StanceMovementProfile* Profile = GetMovementProfile();
	if (!Profile)
	{
		BrakingBand.Reset();
		return Fallback;
	}
	if (bHasMovementInput)
	{
		BrakingBand.Reset();
		return Profile->BrakingDecelerationWithInput;
	}
	if (!BrakingBand.IsSet())
	{
		BrakingBand = Profile->GetBrakingBandForSpeed(GetVelocity().Size2D());
	}
	return Profile->GetGaitSettings(*BrakingBand).BrakingDecelerationNoInput;
}

FVector ABH_CharacterBase::GetGaitSpeeds(EBH_Gait Gait) const
{
	const UBH_StanceMovementProfile* Profile = GetMovementProfile();
	return Profile ? Profile->GetGaitSettings(Gait).Speeds : FVector::ZeroVector;
}

float ABH_CharacterBase::GetMaxAccelerationFor(EBH_Gait Gait, float Speed2D) const
{
	const UBH_StanceMovementProfile* Profile = GetMovementProfile();
	return Profile ? Profile->GetMaxAccelerationFor(Gait, Speed2D) : 0.f;
}

float ABH_CharacterBase::GetBrakingDeceleration(bool bHasMovementInput) const
{
	if (IsCombatMovementLocked())
	{
		return CombatLockBrakingDeceleration;
	}
	const UBH_StanceMovementProfile* Profile = GetMovementProfile();
	if (!Profile)
	{
		return 0.f;
	}
	if (bHasMovementInput)
	{
		return Profile->BrakingDecelerationWithInput;
	}
	const EBH_Gait Band = BrakingBand.IsSet() ? *BrakingBand : Profile->GetBrakingBandForSpeed(GetVelocity().Size2D());
	return Profile->GetGaitSettings(Band).BrakingDecelerationNoInput;
}

// ============================================================================
// Ragdoll (any machine; physics bodies are not replicated, each machine simulates its own copy)
// ============================================================================

void ABH_CharacterBase::StartRagdollLocal(const FVector& InheritVelocity)
{
	if (bRagdollActive)
	{
		return;
	}
	bRagdollActive = true;

	USkeletalMeshComponent* SkelMesh = GetMesh();
	UCapsuleComponent* Capsule = GetCapsuleComponent();
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();

	// Diagnostics (bh.Ragdoll.DebugCamera): what would still block the camera probe if nothing were disabled. Needs the mesh on the capsule.
	if (BH_CharacterBase_Private::CVarRagdollDebugCamera.GetValueOnGameThread() != 0)
	{
		LogRagdollCameraBlockers();
	}

	// Weapons, shields and every other collider first: attached actors can only be found while the mesh is still on the capsule.
	DisableRagdollInterferingCollision();

	// The CMC no longer drives the body; the authority stops replicating movement (each machine simulates its own ragdoll).
	if (MoveComp)
	{
		MoveComp->StopMovementImmediately();
		MoveComp->DisableMovement();
		MoveComp->SetComponentTickEnabled(false);
	}
	if (HasAuthority())
	{
		bRagdollSavedReplicateMovement = IsReplicatingMovement();
		SetReplicateMovement(false);
	}

	if (Capsule)
	{
		RagdollSavedCapsuleCollision = Capsule->GetCollisionEnabled();
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	if (!SkelMesh)
	{
		return;
	}
	if (!SkelMesh->GetPhysicsAsset())
	{
		UE_LOG(LogBHCombat, Warning, TEXT("%s: no physics asset on the mesh, the body cannot ragdoll (it just stops)."), *GetNameSafe(this));
		return;
	}

	RagdollSavedMeshProfile = SkelMesh->GetCollisionProfileName();
	RagdollSavedMeshObjectType = SkelMesh->GetCollisionObjectType();
	RagdollSavedMeshCollision = SkelMesh->GetCollisionEnabled();
	RagdollSavedMeshResponses = SkelMesh->GetCollisionResponseToChannels();
	RagdollSavedMeshScale = SkelMesh->GetRelativeScale3D();

	// Detach BEFORE the simulation starts: a simulating child of the capsule is dragged along by every capsule teleport.
	SkelMesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	bRagdollMeshDetached = true;

	SkelMesh->SetCollisionProfileName(RagdollCollisionProfile);
	// The camera must never collide with the body it is looking at: the Gameplay Cameras CollisionPush node and the spring arm both probe
	// ECC_Camera. The Ragdoll profile already ignores it (DefaultEngine.ini EditProfiles); this makes it independent of the profile data.
	// StopRagdollLocal restores the exact saved responses.
	SkelMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	SkelMesh->SetAllBodiesSimulatePhysics(true);
	SkelMesh->SetSimulatePhysics(true);
	SkelMesh->SetAllBodiesPhysicsBlendWeight(1.f);
	SkelMesh->WakeAllRigidBodies();
	SkelMesh->SetAllPhysicsLinearVelocity(InheritVelocity.GetClampedToMaxSize(FMath::Max(0.f, RagdollMaxInheritSpeed))); // keep the momentum of the fall / run
}

void ABH_CharacterBase::LogRagdollCameraBlockers() const
{
	TArray<const AActor*> ScanActors;
	ScanActors.Add(this);
	TArray<AActor*> CarriedActors;
	GetAttachedActors(CarriedActors, /*bResetArray*/ true, /*bRecursivelyIncludeAttachedActors*/ true);
	for (const AActor* CarriedActor : CarriedActors)
	{
		ScanActors.Add(CarriedActor);
	}

	int32 BlockerCount = 0;
	for (const AActor* ScanActor : ScanActors)
	{
		if (!ScanActor)
		{
			continue;
		}
		TArray<UPrimitiveComponent*> ScanComps;
		ScanActor->GetComponents<UPrimitiveComponent>(ScanComps);
		for (const UPrimitiveComponent* ScanComp : ScanComps)
		{
			if (!ScanComp)
			{
				continue;
			}
			const ECollisionEnabled::Type CollisionMode = ScanComp->GetCollisionEnabled();
			if (CollisionMode == ECollisionEnabled::NoCollision || CollisionMode == ECollisionEnabled::PhysicsOnly)
			{
				continue; // not part of any trace
			}
			if (ScanComp->GetCollisionResponseToChannel(ECC_Camera) == ECR_Ignore)
			{
				continue;
			}
			++BlockerCount;
			UE_LOG(LogBHCombat, Warning, TEXT("%s: ragdoll camera check - %s.%s responds to the Camera channel."), *GetNameSafe(this), *GetNameSafe(ScanActor), *GetNameSafe(ScanComp));
		}
	}
	UE_LOG(LogBHCombat, Log, TEXT("%s: ragdoll camera check - %d component(s) of the pawn and its attached actors respond to the Camera channel (the ragdoll then switches them off or sets them to Ignore)."), *GetNameSafe(this), BlockerCount);
}

void ABH_CharacterBase::StopRagdollLocal(const FTransform* ActorTransformAfterStop)
{
	if (!bRagdollActive)
	{
		return;
	}
	bRagdollActive = false;

	USkeletalMeshComponent* SkelMesh = GetMesh();
	UCapsuleComponent* Capsule = GetCapsuleComponent();
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();

	// Stop the simulation, restore the mesh collision and hang the mesh back on the capsule with its authored offset.
	if (SkelMesh && bRagdollMeshDetached)
	{
		SkelMesh->SetAllBodiesSimulatePhysics(false);
		SkelMesh->SetSimulatePhysics(false);

		if (!RagdollSavedMeshProfile.IsNone() && RagdollSavedMeshProfile != UCollisionProfile::CustomCollisionProfileName)
		{
			SkelMesh->SetCollisionProfileName(RagdollSavedMeshProfile);
		}
		else
		{
			SkelMesh->SetCollisionObjectType(RagdollSavedMeshObjectType);
			SkelMesh->SetCollisionResponseToChannels(RagdollSavedMeshResponses);
		}
		SkelMesh->SetCollisionEnabled(RagdollSavedMeshCollision);

		if (Capsule)
		{
			SkelMesh->AttachToComponent(Capsule, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
			SkelMesh->SetRelativeLocationAndRotation(GetBaseTranslationOffset(), GetBaseRotationOffset());
			SkelMesh->SetRelativeScale3D(RagdollSavedMeshScale);
		}
	}
	bRagdollMeshDetached = false;

	// Capsule still off here: the move to the return point cannot pop out of the floor.
	if (ActorTransformAfterStop)
	{
		SetActorLocationAndRotation(ActorTransformAfterStop->GetLocation(), ActorTransformAfterStop->Rotator(), false, nullptr, ETeleportType::TeleportPhysics);
	}
	if (Capsule)
	{
		Capsule->SetCollisionEnabled(RagdollSavedCapsuleCollision);
	}
	RestoreRagdollInterferingCollision();

	if (MoveComp)
	{
		MoveComp->SetComponentTickEnabled(true); // the movement mode is the caller's job
	}
	if (HasAuthority())
	{
		SetReplicateMovement(bRagdollSavedReplicateMovement);
	}
}

void ABH_CharacterBase::DisableRagdollInterferingCollision()
{
	RagdollSavedPrimitives.Reset();

	auto SaveAndDisable = [this](UPrimitiveComponent* CollisionComp)
	{
		if (CollisionComp && CollisionComp->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
		{
			FBH_RagdollSavedPrimitive& Saved = RagdollSavedPrimitives.AddDefaulted_GetRef();
			Saved.Component = CollisionComp;
			Saved.Collision = CollisionComp->GetCollisionEnabled();
			CollisionComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	};

	// Weapons, shields, ... attached to the character (recursively). Their colliders fight the ragdoll bodies (explosive depenetration).
	TArray<AActor*> CarriedActors;
	GetAttachedActors(CarriedActors, /*bResetArray*/ true, /*bRecursivelyIncludeAttachedActors*/ true);
	TArray<UPrimitiveComponent*> CollisionComps;
	for (AActor* CarriedActor : CarriedActors)
	{
		if (!CarriedActor)
		{
			continue;
		}
		CarriedActor->GetComponents<UPrimitiveComponent>(CollisionComps);
		for (UPrimitiveComponent* CollisionComp : CollisionComps)
		{
			SaveAndDisable(CollisionComp);
		}
	}

	// The character's own non-mesh primitives (weapon meshes, hit boxes, ...); the capsule is handled by the caller.
	GetComponents<UPrimitiveComponent>(CollisionComps);
	for (UPrimitiveComponent* CollisionComp : CollisionComps)
	{
		if (CollisionComp != GetMesh() && CollisionComp != GetCapsuleComponent())
		{
			SaveAndDisable(CollisionComp);
		}
	}
}

void ABH_CharacterBase::RestoreRagdollInterferingCollision()
{
	for (const FBH_RagdollSavedPrimitive& Saved : RagdollSavedPrimitives)
	{
		if (UPrimitiveComponent* SavedComp = Saved.Component.Get())
		{
			SavedComp->SetCollisionEnabled(Saved.Collision);
		}
	}
	RagdollSavedPrimitives.Reset();
}
