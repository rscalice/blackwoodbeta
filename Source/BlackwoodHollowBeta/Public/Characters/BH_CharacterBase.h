// Blackwood Hollow - native base for the motion-matching character (GASP CMC sandbox character reparents onto this)
//
// Owns the AbilitySystemComponent + UAH_AttributeSet (on the pawn, like ABH_EnemyBase), the replicated weapon
// stance component, replicated lock-on strafe and a gait mirror for native consumers.
// The constructor only creates subobjects: every mesh / movement default stays on the Blueprint CDO.
//
// Phase 10B part 2: also owns the UBH_PlayerDeathComponent (player death, replicated ragdoll, revive window, hub respawn, revive).
// It stays dormant on AI combatants (pawns with a UBH_CombatIdentityComponent). While the pawn is down or getting up,
// IsCombatMovementLocked() is true, and ReviveInputAction (assign an Interact Input Action on the Blueprint) is bound to
// hold-to-revive for the local player.
//
// Shared ragdoll: StartRagdollLocal / StopRagdollLocal turn the body into a physics ragdoll on the machine that calls them (physics
// bodies are not replicated: every machine simulates its own copy). The simulating mesh is DETACHED from the capsule first, so moving
// the capsule afterwards (the player camera follow, corrections, teleports) never drags the bodies. Used by UBH_PlayerDeathComponent
// (player) and UBH_CombatIdentityComponent (motion-matching enemies).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "GenericTeamAgentInterface.h"
#include "GameplayTagContainer.h"
#include "Engine/EngineTypes.h"
#include "Combat/BH_CombatTeam.h"
#include "Characters/BH_CharacterTypes.h"
#include "BH_CharacterBase.generated.h"

class UAbilitySystemComponent;
class UAH_AttributeSet;
class UBH_StanceComponent;
class UBH_StanceMovementProfile;
class UBH_PlayerDeathComponent;
class UBH_InteractorComponent;
class UBH_ArmorVisualComponent;
class UBH_WeaponVisualComponent;
class UGameplayAbility;
class UInputAction;
class UInputMappingContext;
class UPrimitiveComponent;

/** One primitive whose collision the ragdoll switched off (StopRagdollLocal puts it back). */
struct FBH_RagdollSavedPrimitive
{
	TWeakObjectPtr<UPrimitiveComponent> Component;
	TEnumAsByte<ECollisionEnabled::Type> Collision = ECollisionEnabled::QueryAndPhysics;
};

UCLASS(Abstract, Blueprintable)
class BLACKWOODHOLLOWBETA_API ABH_CharacterBase : public ACharacter, public IAbilitySystemInterface, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	ABH_CharacterBase(const FObjectInitializer& ObjectInitializer);

	// -- IAbilitySystemInterface ------------------------------------------------
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override { return AbilitySystemComponent; }

	UFUNCTION(BlueprintPure, Category = "BH|Combat")
	UAH_AttributeSet* GetAttributeSet() const { return AttributeSet; }

	UFUNCTION(BlueprintPure, Category = "BH|Stance")
	UBH_StanceComponent* GetStanceComponent() const { return StanceComponent; }

	UFUNCTION(BlueprintPure, Category = "BH|Death")
	UBH_PlayerDeathComponent* GetDeathComponent() const { return DeathComponent; }

	/** Phase 11C: the player's side of interaction (focus scan, prompt, hold RPCs). Only does anything on a locally controlled player pawn. */
	UFUNCTION(BlueprintPure, Category = "BH|Interaction")
	UBH_InteractorComponent* GetInteractorComponent() const { return InteractorComponent; }

	/** Phase 11E: builds the equipped armor (or the starting outfit) on the MetaHuman visual body, locally on every machine. Cosmetic only. */
	UFUNCTION(BlueprintPure, Category = "BH|Armor")
	UBH_ArmorVisualComponent* GetArmorVisualComponent() const { return ArmorVisualComponent; }

	// -- IGenericTeamAgentInterface (replicated so clients resolve teams without a controller) --
	virtual FGenericTeamId GetGenericTeamId() const override { return BH_CombatTeam::ToGenericTeamId(CombatTeam); }
	virtual void SetGenericTeamId(const FGenericTeamId& NewTeamId) override { CombatTeam = BH_CombatTeam::FromGenericTeamId(NewTeamId); }

	/** Neutral falls through to the PlayerState / controller team resolution in UBH_CombatFunctionLibrary::GetCombatTeamId. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "BH|Combat")
	EBH_CombatTeam CombatTeam = EBH_CombatTeam::Neutral;

	/** Granted once on authority in BeginPlay. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Combat")
	TArray<TSubclassOf<UGameplayAbility>> DefaultAbilities;

	// -- Movement seams ---------------------------------------------------------

	/** Lock-on forces strafe rotation. OR'ed into the BP's WantsToStrafe/WantsToAim test in UpdateRotation_PreCMC. */
	UPROPERTY(ReplicatedUsing = OnRep_LockOnStrafe, BlueprintReadOnly, Category = "BH|Movement")
	bool bLockOnStrafe = false;

	/** Local change applies immediately; a non-authority caller also asks the server (server value wins on replication). */
	UFUNCTION(BlueprintCallable, Category = "BH|Movement")
	void SetLockOnStrafe(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "BH|Movement")
	bool IsLockOnStrafeActive() const { return bLockOnStrafe; }

	UFUNCTION(BlueprintPure, Category = "BH|Movement")
	EBH_RotationMode GetRotationMode() const;

	/** Called by the BP right after it sets its own Gait variable (E_Gait byte). */
	UFUNCTION(BlueprintCallable, Category = "BH|Movement")
	void SetGaitFromByte(uint8 GaitByte);

	UFUNCTION(BlueprintPure, Category = "BH|Movement")
	EBH_Gait GetGait() const { return CurrentGait; }

	UFUNCTION(BlueprintPure, Category = "BH|Stance")
	FGameplayTag GetWeaponStance() const;

	// -- Per-stance movement profile (the BP passes its own value as Fallback when no profile is set) --

	/** Active profile from the stance component, or nullptr. */
	UFUNCTION(BlueprintPure, Category = "BH|Movement")
	UBH_StanceMovementProfile* GetMovementProfile() const;

	/** Profile speeds for CurrentGait (set by SetGaitFromByte earlier in the same tick). */
	UFUNCTION(BlueprintPure, Category = "BH|Movement")
	FVector GetGaitSpeedsOr(FVector Fallback) const;

	UFUNCTION(BlueprintPure, Category = "BH|Movement")
	FVector GetCrouchSpeedsOr(FVector Fallback) const;

	UFUNCTION(BlueprintPure, Category = "BH|Movement")
	float GetMaxAccelerationOr(float Fallback) const;

	UFUNCTION(BlueprintPure, Category = "BH|Movement")
	float GetGroundFrictionOr(float Fallback) const;

	// -- AI gait (GASP reads IA_Move for the desired gait, which is zero for AI) --------------------------------------

	/** Gait the AI controller wants (Walk close to the target, Run, Sprint far away). Replicated so simulated proxies pick the same locomotion rows. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "BH|Movement")
	EBH_Gait AIDesiredGait = EBH_Gait::Run;

	/** Authority only. */
	UFUNCTION(BlueprintCallable, Category = "BH|Movement")
	void SetAIDesiredGait(EBH_Gait NewGait);

	UFUNCTION(BlueprintPure, Category = "BH|Movement")
	EBH_Gait GetAIDesiredGait() const { return AIDesiredGait; }

	// -- Combat movement lock (State.Combat.MovementLocked, ActivationOwnedTags of melee attacks / hit reactions) --

	/**
	 * True while the ASC carries State.Combat.MovementLocked or State.Combat.Dead, or the death component says the pawn is down / getting up:
	 * profile speeds / acceleration read 0, input is ignored and braking is CombatLockBrakingDeceleration. Root motion is unaffected.
	 */
	UFUNCTION(BlueprintPure, Category = "BH|Movement")
	bool IsCombatMovementLocked() const;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Movement")
	float CombatLockBrakingDeceleration = 2000.f;

	/** Interact input: hold to revive a downed party member (Started -> StartRevive, Completed / Canceled -> StopRevive). Must also be mapped in the active Input Mapping Context. Empty = bind nothing. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Death")
	TObjectPtr<UInputAction> ReviveInputAction;

	/**
	 * Mapping context holding ONLY the revive input (IA_Interact on E and gamepad X). UBH_PlayerDeathComponent adds it to the LOCAL player's
	 * Enhanced Input subsystem at ReviveMappingPriority while a downed party member is in revive range, and removes it otherwise. Because it
	 * outranks the combat context (which no longer maps IA_Interact), IA_Interact consumes gamepad X while it is active, so X revives near a
	 * downed partner and is Shield Bash everywhere else. Empty = the revive context is never added.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Death")
	TSoftObjectPtr<UInputMappingContext> ReviveMappingContext = TSoftObjectPtr<UInputMappingContext>(FSoftObjectPath(TEXT("/Game/Input/IMC_Revive.IMC_Revive")));

	/** Priority ReviveMappingContext is added with. Must be strictly higher than the combat context (PC_BlackwoodHollow adds IMC_BlackwoodCombat at 1). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Death")
	int32 ReviveMappingPriority = 10;

	/** Not pure: updates the braking band latch. Call once per tick (from CalculateBrakingDeceleration). */
	UFUNCTION(BlueprintCallable, Category = "BH|Movement")
	float ComputeBrakingDecelerationOr(bool bHasMovementInput, float Fallback);

	/** Explicit-gait variants for other callers (AI, HUD). Zero when no profile. */
	UFUNCTION(BlueprintPure, Category = "BH|Movement")
	FVector GetGaitSpeeds(EBH_Gait Gait) const;

	UFUNCTION(BlueprintPure, Category = "BH|Movement")
	float GetMaxAccelerationFor(EBH_Gait Gait, float Speed2D) const;

	/** Latched band, no side effects. Zero when no profile. */
	UFUNCTION(BlueprintPure, Category = "BH|Movement")
	float GetBrakingDeceleration(bool bHasMovementInput) const;

	/**
	 * Owning-client twin of a server pushback (UBH_CombatFeelLibrary::ApplyHitPushback): applies the identical constant-force root
	 * motion source (same id / force / duration) so the autonomous proxy's prediction agrees with the server and is not corrected
	 * for it. Unreliable: a lost packet just means a small server correction.
	 */
	UFUNCTION(Client, Unreliable)
	void Client_ApplyHitPushback(FVector Direction, float Distance, float Duration, uint16 Id);

	// -- Ragdoll (any machine; each machine simulates its own copy of the body) -----------------------------------

	/**
	 * Switches this machine's copy of the body to physics: stops and disables the CMC, turns the capsule off, DETACHES the mesh from the
	 * capsule (KeepWorldTransform) before simulation starts, switches the mesh to RagdollCollisionProfile and simulates the physics
	 * asset with InheritVelocity (clamped to RagdollMaxInheritSpeed). Collision of every actor attached to the character (weapons,
	 * shields; found recursively) and of the character's own non-mesh primitives is disabled and remembered. On authority it also stops
	 * movement replication (restored by StopRagdollLocal). Idempotent. Read the velocity you want to keep BEFORE calling (the CMC is zeroed).
	 */
	void StartRagdollLocal(const FVector& InheritVelocity);

	/**
	 * Undoes StartRagdollLocal: stops the simulation, restores the mesh collision, re-attaches the mesh to the capsule with the authored
	 * base offset, optionally teleports the actor to *ActorTransformAfterStop (location + yaw; capsule still off, so no depenetration pop),
	 * restores the capsule and attached-actor collision and re-enables the CMC tick. The movement MODE is the caller's job (CMC is left in
	 * MOVE_None). No-op when not ragdolling.
	 */
	void StopRagdollLocal(const FTransform* ActorTransformAfterStop = nullptr);

	/** True between StartRagdollLocal and StopRagdollLocal on this machine. (Not "IsRagdolling": the GASP Blueprint already has a variable of that name.) */
	bool IsRagdollActive() const { return bRagdollActive; }

	/** Mesh collision profile while ragdolling. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Ragdoll")
	FName RagdollCollisionProfile = TEXT("Ragdoll");

	/** The velocity handed to the ragdoll is clamped to this speed (a sprinting death must not launch the body). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Ragdoll", meta = (ClampMin = "0.0", ForceUnits = "cm/s"))
	float RagdollMaxInheritSpeed = 600.f;

protected:
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;
	virtual void BeginPlay() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Input is ignored while IsCombatMovementLocked (unless forced), so the ABP never sees a start / pivot during a swing. */
	virtual void AddMovementInput(FVector WorldDirection, float ScaleValue = 1.0f, bool bForce = false) override;

	UFUNCTION(Server, Reliable)
	void ServerSetLockOnStrafe(bool bEnabled);

	/** Hook for future use: the BP reads bLockOnStrafe every tick. */
	UFUNCTION()
	void OnRep_LockOnStrafe();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BH|Combat")
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BH|Combat")
	TObjectPtr<UAH_AttributeSet> AttributeSet;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BH|Stance")
	TObjectPtr<UBH_StanceComponent> StanceComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BH|Death")
	TObjectPtr<UBH_PlayerDeathComponent> DeathComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BH|Interaction")
	TObjectPtr<UBH_InteractorComponent> InteractorComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BH|Armor")
	TObjectPtr<UBH_ArmorVisualComponent> ArmorVisualComponent;

	/** Phase 12F-2 (#24): weapon visuals on the MetaHuman visual body. Cosmetic, every machine but a dedicated server. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BH|Weapon")
	TObjectPtr<UBH_WeaponVisualComponent> WeaponVisualComponent;

private:
	/** Idempotent. */
	void InitAbilitySystem();
	void GrantDefaultAbilities();

	UPROPERTY(Transient, VisibleInstanceOnly, Category = "BH|Movement")
	EBH_Gait CurrentGait = EBH_Gait::Run;

	bool bAbilitiesGranted = false;

	/** Speed band latched on the first no-input tick of a stop (Gait flips on release, so it cannot be used). */
	TOptional<EBH_Gait> BrakingBand;

	// -- Ragdoll state ---------------------------------------------------------------------------------------------
	void DisableRagdollInterferingCollision();
	void RestoreRagdollInterferingCollision();

	/** Diagnostics for bh.Ragdoll.DebugCamera: logs the pawn's / attached actors' components that currently respond to the Camera channel (call while the mesh is still on the capsule). */
	void LogRagdollCameraBlockers() const;

	bool bRagdollActive = false;
	bool bRagdollMeshDetached = false;
	bool bRagdollSavedReplicateMovement = true;

	/** Mesh state before the ragdoll (restored exactly). */
	FName RagdollSavedMeshProfile;
	TEnumAsByte<ECollisionChannel> RagdollSavedMeshObjectType = ECC_Pawn;
	TEnumAsByte<ECollisionEnabled::Type> RagdollSavedMeshCollision = ECollisionEnabled::QueryOnly;
	FCollisionResponseContainer RagdollSavedMeshResponses;
	FVector RagdollSavedMeshScale = FVector::OneVector;
	TEnumAsByte<ECollisionEnabled::Type> RagdollSavedCapsuleCollision = ECollisionEnabled::QueryAndPhysics;

	/** Attached actors' and own non-mesh primitives whose collision was switched off. */
	TArray<FBH_RagdollSavedPrimitive> RagdollSavedPrimitives;
};
