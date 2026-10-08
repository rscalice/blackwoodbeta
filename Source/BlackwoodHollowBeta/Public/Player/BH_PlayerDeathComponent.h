// Blackwood Hollow - player death, replicated ragdoll, revive window, hub respawn, player-to-player revive (Phase 10B part 2)
// Target: Unreal Engine 5.8 (C++), GAS
//
// A default subobject of ABH_CharacterBase (so BP_BH_PlayerCharacter has it without any Blueprint edit). It is DORMANT on anything
// that is not a player: pawns with a UBH_CombatIdentityComponent (AI combatants) are ignored when their Dead tag arrives.
// The pawn is NEVER destroyed or unpossessed: it stays the body, and the same pawn is revived / teleported, so the ASC, attribute
// set, inventory, stance, weapons, HUD, input mapping and (PlayerState) progression all carry over untouched.
//
// FLOW (everything below is decided on the SERVER; clients only mirror DeathState)
//   1. Health hits 0 -> UAH_AttributeSet adds the replicated loose tag State.Combat.Dead. The server sees it (tag event), cancels all
//      abilities and publishes DeathState.Phase = Dead. Every ability already refuses to activate while Dead (fragments too),
//      and ABH_CharacterBase::IsCombatMovementLocked() is true while the phase is not Alive (input ignored, speeds 0).
//   2. RAGDOLL (every machine, including a listen-server host, starts it from DeathState) through the shared
//      ABH_CharacterBase::StartRagdollLocal: the mesh is DETACHED from the capsule (so the per-tick capsule follow below can never drag
//      the bodies), the capsule stops colliding, the CMC is switched off, attached weapon colliders are disabled and the mesh simulates
//      its physics asset, inheriting the pawn's (capped) velocity. Physics bodies are NOT replicated:
//      each machine simulates its own copy, and the capsule follows the local pelvis so the camera stays on the body. The server
//      samples the pelvis once it has settled (slow for SettleHoldSeconds, or SettleTimeout) and publishes
//        DeathState.BodyRestLocation / bBodyFaceUp / GetUpYaw / ReviveLocation / bBodySettled
//      Clients then (once their own sim has settled too) shift their ragdoll by the difference and snap the capsule to the same
//      point, so every machine ends with the body at BodyRestLocation. Late join / relevancy: a pawn that arrives with the state
//      already Dead starts the ragdoll in BeginPlay and corrects the same way. Range checks and revive-in-place use the rest point.
//   3. COMBAT: the revive window opens (Phase Dead -> AwaitingChoice) once the body has settled AND the party is out of combat
//      (UBH_PartyStateSubsystem, OutOfCombatGrace 3 s) or there is no other living player (solo / wipe: immediately).
//   4. CHOICE (the dead player, Server RPCs): RequestRespawnAtHub -> teleport to the nearest ABH_RespawnPoint (full health);
//      RequestWaitForRevive -> WaitingForRevive (a hub respawn is still possible later).
//   5. REVIVE (a living player within ReviveRange of BodyRestLocation holds Interact for ReviveHoldSeconds): StartRevive /
//      StopRevive on the reviver's component (ABH_CharacterBase binds ReviveInputAction to them). Server-validated every tick;
//      interrupted by range, damage to the reviver, or the party re-entering combat. Progress replicates in ReviveInfo.
//   6. COMING BACK (revive or hub respawn): ResetBlight(), Dead / PostureBroken removed, Health set (ReviveHealthPercent /
//      RespawnHealthPercent of MaxHealth), Posture and Stamina refilled; every machine un-ragdolls (ABH_CharacterBase::StopRagdollLocal:
//      mesh re-attached, collision and CMC restored), snaps to DeathState.ReviveLocation and plays the GASP get-up montage while movement stays locked.
//
// Camera while dead: stays on the own body (the capsule follows the pelvis). The live camera is the Gameplay Cameras system
// (DDCVar.NewGameplayCameraSystem.Enable): CameraRig_CollisionOffset pushes the camera toward a safe point that sits CameraRigSafeOffset
// (Pawn space) from the capsule, probing ECC_Camera. ComputeCameraAnchor keeps that safe point out of walls / ceilings for the LOCAL pawn
// (Phase 11A-1; bh.Ragdoll.SafeCameraAnchor 0 turns it off). TODO(spectate): switching to a partner would be a
// PlayerController::SetViewTargetWithBlend in EnterDeathLocal / back in ExitDeathLocal.
//
// Debug: bh.Player.Kill / bh.Player.Revive (BH_RPGDebugCommands.cpp) go through ServerDebugKill / ServerDebugRevive.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/TimerHandle.h"
#include "Engine/EngineTypes.h"
#include "Animation/AnimEnums.h"
#include "GameplayTagContainer.h"
#include "Engine/NetSerialization.h"
#include "BH_PlayerDeathComponent.generated.h"

class AActor;
class APawn;
class ACharacter;
class UAbilitySystemComponent;
class UAnimMontage;
class UEnhancedInputLocalPlayerSubsystem;
class UInputMappingContext;
class USkeletalMeshComponent;
struct FOnAttributeChangeData;

UENUM(BlueprintType)
enum class EBH_DeathPhase : uint8
{
	/** Alive (also during the get-up). */
	Alive,

	/** Down; waiting for the body to settle and the party to leave combat. */
	Dead UMETA(DisplayName = "Dead (waiting for out of combat)"),

	/** The revive window is open and the player has not chosen yet. */
	AwaitingChoice,

	/** The player chose to wait for a party member; a hub respawn is still available. */
	WaitingForRevive,
};

/** Replicated death state of one pawn (one struct, one OnRep: the fields always arrive together). */
USTRUCT()
struct FBH_DeathRepState
{
	GENERATED_BODY()

	UPROPERTY()
	EBH_DeathPhase Phase = EBH_DeathPhase::Alive;

	/** Bumped on every death and every return to life, so a death/revive pair that coalesces into one update is still seen. */
	UPROPERTY()
	uint8 Epoch = 0;

	/** The server sampled the body at rest; BodyRestLocation / bBodyFaceUp / GetUpYaw / ReviveLocation are valid. */
	UPROPERTY()
	bool bBodySettled = false;

	/** The body lies on its back (selects the get-up montage). */
	UPROPERTY()
	bool bBodyFaceUp = false;

	/** Pelvis position at rest (revive range checks). */
	UPROPERTY()
	FVector_NetQuantize10 BodyRestLocation = FVector::ZeroVector;

	/** Where the capsule goes when the pawn comes back: the floor under the body, or the hub spawn point. */
	UPROPERTY()
	FVector_NetQuantize10 ReviveLocation = FVector::ZeroVector;

	/** Yaw (degrees) the pawn faces after the get-up. */
	UPROPERTY()
	float GetUpYaw = 0.f;

	/** Hub the "Respawn at ..." choice leads to (set when the window opens). */
	UPROPERTY()
	FName HubName;
};

/** Who is reviving this (dead) pawn, and how far along. */
USTRUCT()
struct FBH_ReviveRepInfo
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<APawn> Reviver = nullptr;

	/** 0..100 in steps of 2. */
	UPROPERTY()
	uint8 ProgressPct = 0;
};

/** The death phase changed (server and every client). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FBH_OnDeathPhaseChanged, EBH_DeathPhase, NewPhase, EBH_DeathPhase, OldPhase);

/**
 * Revive progress changed. Fires on the machine of BOTH players:
 *   bIsDeadPlayerView = true  -> this pawn is the one being revived (OtherPlayer = the reviver)
 *   bIsDeadPlayerView = false -> this pawn is the reviver (OtherPlayer = the body being revived)
 * OtherPlayer null = the revive stopped (cancelled / finished / nobody reviving). Fraction is 0..1.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FBH_OnReviveProgress, float, Fraction, bool, bIsDeadPlayerView, APawn*, OtherPlayer);

UCLASS(ClassGroup = (BlackwoodHollow), meta = (BlueprintSpawnableComponent))
class BLACKWOODHOLLOWBETA_API UBH_PlayerDeathComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBH_PlayerDeathComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** The death component on Actor, or null. */
	UFUNCTION(BlueprintPure, Category = "BH|Death")
	static UBH_PlayerDeathComponent* Find(const AActor* Actor);

	/** True for a player pawn that is alive: Phase Alive and no State.Combat.Dead tag. */
	static bool IsLivingPlayer(const AActor* Actor);

	// -- Tunables: revive ---------------------------------------------------------------------

	/** A living player within this distance of the body's rest location can revive it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Death|Revive", meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float ReviveRange = 200.f;

	/** Extra range the SERVER tolerates before it cancels a revive (latency / the reviver being nudged). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Death|Revive", meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float ReviveRangeServerTolerance = 50.f;

	/** Seconds the reviver must hold Interact. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Death|Revive", meta = (ClampMin = "0.1", ForceUnits = "s"))
	float ReviveHoldSeconds = 3.f;

	/** Health (fraction of MaxHealth) a player revived in place comes back with. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Death|Revive", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float ReviveHealthPercent = 0.5f;

	/** Health (fraction of MaxHealth) a player who respawns at the hub comes back with. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Death|Revive", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float RespawnHealthPercent = 1.f;

	/** The reviver taking damage cancels the revive. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Death|Revive")
	bool bInterruptReviveOnDamage = true;

	// -- Tunables: revive input context -------------------------------------------------------

	/** Seconds between checks of "downed partner in range" on the locally controlled pawn (drives the revive mapping context). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Death|Revive", meta = (ClampMin = "0.02", ForceUnits = "s"))
	float ReviveContextPollInterval = 0.1f;

	/** Do not take the revive input (gamepad X) while the party is in combat: the server refuses a revive then, so X stays Shield Bash. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Death|Revive")
	bool bReviveContextOnlyOutOfCombat = true;

	// -- Tunables: ragdoll --------------------------------------------------------------------

	/** Body whose position is the "body location" (ragdoll tracking, rest sample, revive range). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Death|Ragdoll")
	FName PelvisBoneName = TEXT("pelvis");

	/** Used with the pelvis to find which way the body lies (get-up facing). Falls back to the actor yaw when missing. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Death|Ragdoll")
	FName HeadBoneName = TEXT("head");

	/** Pelvis speed (cm/s) below which the body counts as at rest. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Death|Ragdoll", meta = (ClampMin = "0.0", ForceUnits = "cm/s"))
	float SettleSpeedThreshold = 15.f;

	/** Seconds the pelvis must stay under SettleSpeedThreshold. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Death|Ragdoll", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float SettleHoldSeconds = 0.4f;

	/** Never settle before this many seconds after death (the first frames of a ragdoll are still). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Death|Ragdoll", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float SettleMinSeconds = 0.75f;

	/** Settle at the latest this many seconds after death, whatever the body is doing. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Death|Ragdoll", meta = (ClampMin = "0.1", ForceUnits = "s"))
	float SettleTimeout = 2.f;

	/** A client whose own ragdoll rests closer than this to the server's rest point (cm) is not shifted, only the capsule snaps. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Death|Ragdoll", meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float ClientCorrectionTolerance = 12.f;

	/** Floor search above / below the body when computing ReviveLocation. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Death|Ragdoll", meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float GroundTraceUp = 100.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Death|Ragdoll", meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float GroundTraceDown = 400.f;

	// -- Tunables: dead camera (local player only, never replicated) ---------------------------

	/** The CollisionPush node's SafePositionOffset in CameraRig_CollisionOffset (Pawn space). The camera is pushed toward capsule + this offset, so it must stay out of geometry. Keep in sync with that rig. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Death|Camera")
	FVector CameraRigSafeOffset = FVector(0.f, 20.f, 150.f);

	/** Radius of the sweep that checks the safe point (the rig's CollisionSphereRadius). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Death|Camera", meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float CameraProbeRadius = 10.f;

	/** Gap kept between the safe point and the geometry that blocks it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Death|Camera", meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float CameraAnchorMargin = 15.f;

	// -- Tunables: get-up ---------------------------------------------------------------------

	/** Get-up played when the body lies face down (GASP's ragdoll get-up from the front). Verify F/B against your lying pose. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Death|GetUp")
	TSoftObjectPtr<UAnimMontage> GetUpFaceDownMontage = TSoftObjectPtr<UAnimMontage>(FSoftObjectPath(TEXT("/Game/Skeletons/UEFN_Mannequin/Animations/Ragdoll/AM_M_ragdoll_getup_stand_F.AM_M_ragdoll_getup_stand_F")));

	/** Get-up played when the body lies face up (GASP's ragdoll get-up from the back). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Death|GetUp")
	TSoftObjectPtr<UAnimMontage> GetUpFaceUpMontage = TSoftObjectPtr<UAnimMontage>(FSoftObjectPath(TEXT("/Game/Skeletons/UEFN_Mannequin/Animations/Ragdoll/AM_M_ragdoll_getup_stand_B.AM_M_ragdoll_getup_stand_B")));

	/** Seconds movement stays locked after coming back (the get-up montage is cut to this length). Also the lock when no montage plays. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Death|GetUp", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float GetUpDurationSeconds = 2.f;

	/** Root motion mode restored on the mesh anim instance after the get-up (it is set to Ignore while the montage plays, so the capsule is never dragged). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Death|GetUp")
	TEnumAsByte<ERootMotionMode::Type> PostGetUpRootMotionMode = ERootMotionMode::RootMotionFromMontagesOnly;

	// -- State (any machine) ------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "BH|Death")
	EBH_DeathPhase GetPhase() const { return DeathState.Phase; }

	UFUNCTION(BlueprintPure, Category = "BH|Death")
	bool IsDown() const { return DeathState.Phase != EBH_DeathPhase::Alive; }

	/** The revive window is open: the dead player may choose (or change a "wait" into a hub respawn). */
	UFUNCTION(BlueprintPure, Category = "BH|Death")
	bool IsWindowOpen() const { return DeathState.Phase == EBH_DeathPhase::AwaitingChoice || DeathState.Phase == EBH_DeathPhase::WaitingForRevive; }

	/** Down or still getting up: the movement lock read by ABH_CharacterBase::IsCombatMovementLocked. */
	UFUNCTION(BlueprintPure, Category = "BH|Death")
	bool IsMovementBlocked() const { return DeathState.Phase != EBH_DeathPhase::Alive || bGettingUp; }

	UFUNCTION(BlueprintPure, Category = "BH|Death")
	bool IsBodySettled() const { return DeathState.bBodySettled; }

	/** Where the body lies at rest (valid when IsBodySettled; otherwise the pawn's current location). */
	UFUNCTION(BlueprintPure, Category = "BH|Death")
	FVector GetBodyRestLocation() const;

	/** Hub the respawn choice leads to ("PortVanguard"); None until the window opens. */
	UFUNCTION(BlueprintPure, Category = "BH|Death")
	FName GetRespawnHubName() const { return DeathState.HubName; }

	/** The hub name as a readable label ("Port Vanguard"). */
	UFUNCTION(BlueprintPure, Category = "BH|Death")
	FString GetRespawnHubLabel() const;

	/** 0..1 revive progress of THIS body (replicated). */
	UFUNCTION(BlueprintPure, Category = "BH|Death|Revive")
	float GetReviveProgress() const { return static_cast<float>(ReviveInfo.ProgressPct) / 100.f; }

	/** Somebody is reviving this body right now. */
	UFUNCTION(BlueprintPure, Category = "BH|Death|Revive")
	bool IsBeingRevived() const { return ReviveInfo.Reviver != nullptr; }

	// -- The dead player's choice (local call -> Server RPC) ------------------------------------

	/** Respawn at the nearest hub. Allowed while the window is open (AwaitingChoice or WaitingForRevive). */
	UFUNCTION(BlueprintCallable, Category = "BH|Death")
	void RequestRespawnAtHub();

	/** Stay down and wait for a party member. Allowed while AwaitingChoice. */
	UFUNCTION(BlueprintCallable, Category = "BH|Death")
	void RequestWaitForRevive();

	// -- The reviver ---------------------------------------------------------------------------

	/** The nearest body this pawn could revive right now (window open, in ReviveRange, not already being revived by someone else), or null. */
	UFUNCTION(BlueprintPure, Category = "BH|Death|Revive")
	APawn* FindReviveCandidate() const;

	/** Starts reviving the nearest candidate (hold-to-revive: call StopRevive on release). Local call on the owning client / host. @return a candidate was found and the request was sent. */
	UFUNCTION(BlueprintCallable, Category = "BH|Death|Revive")
	bool StartRevive();

	/** Releases the Interact key: cancels an unfinished revive. */
	UFUNCTION(BlueprintCallable, Category = "BH|Death|Revive")
	void StopRevive();

	/** Input glue (ABH_CharacterBase binds ReviveInputAction to these). */
	void OnReviveInputPressed() { StartRevive(); }
	void OnReviveInputReleased() { StopRevive(); }

	/**
	 * SERVER. Brings this pawn back in place (at DeathState.ReviveLocation, sampling the body first if it has not settled)
	 * with HealthFraction of MaxHealth. No-op when alive. The reviver's hold completing calls this; so does bh.Player.Revive.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "BH|Death")
	void ReviveInPlace(float HealthFraction);

	/**
	 * SERVER. Opens the revive window when everything allows it: Phase Dead, body settled, and the party out of combat
	 * (or no other living player). Called when the body settles, when the party leaves combat and when the player dies.
	 */
	void TryOpenReviveWindow();

	// -- Events ---------------------------------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "BH|Death")
	FBH_OnDeathPhaseChanged OnDeathPhaseChanged;

	UPROPERTY(BlueprintAssignable, Category = "BH|Death|Revive")
	FBH_OnReviveProgress OnReviveProgressChanged;

	// -- Debug (non-shipping; the bh.Player.* commands) -------------------------------------------

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerDebugKill();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerDebugRevive();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Cosmetic hook on every machine: the ragdoll started. */
	UFUNCTION(BlueprintImplementableEvent, Category = "BH|Death", meta = (DisplayName = "On Ragdoll Started"))
	void K2_OnRagdollStarted();

	/** Cosmetic hook on every machine: the pawn stood up (bFaceUp = which get-up was chosen). */
	UFUNCTION(BlueprintImplementableEvent, Category = "BH|Death", meta = (DisplayName = "On Get Up Started"))
	void K2_OnGetUpStarted(bool bFaceUp);

protected:
	// -- Replication ----------------------------------------------------------------------------
	UPROPERTY(ReplicatedUsing = OnRep_DeathState)
	FBH_DeathRepState DeathState;

	UPROPERTY(ReplicatedUsing = OnRep_ReviveInfo)
	FBH_ReviveRepInfo ReviveInfo;

	UFUNCTION()
	void OnRep_DeathState();

	UFUNCTION()
	void OnRep_ReviveInfo();

	UFUNCTION(Server, Reliable)
	void ServerRespawnAtHub();

	UFUNCTION(Server, Reliable)
	void ServerWaitForRevive();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerStartRevive(APawn* Target);

	UFUNCTION(Server, Reliable)
	void ServerStopRevive();

	UFUNCTION()
	void HandlePartyCombatChanged(bool bInCombat);

private:
	// -- Shared (all machines) --------------------------------------------------------------------
	void ApplyState();
	void EnterDeathLocal();
	void ExitDeathLocal();
	void TickRagdoll(float DeltaTime);
	void ApplyBodyCorrection();

	/**
	 * Capsule location to use for a body at BodyLocation so the camera rig's safe point (capsule + CameraRigSafeOffset) is not inside a wall,
	 * floor or ceiling. Only the locally controlled pawn is adjusted (the camera is local); everyone else gets BodyLocation back.
	 */
	FVector ComputeCameraAnchor(const FVector& BodyLocation) const;
	void StartGetUp(bool bFaceUp);
	void EndGetUp();
	void RestoreRootMotionMode();
	void RefreshTickState();
	void BroadcastReviveProgress();
	bool GetPelvisState(FVector& OutLocation, FVector& OutVelocity, FQuat& OutRotation) const;
	ACharacter* GetCharacterOwner() const;
	UAbilitySystemComponent* GetOwnerASC() const;
	USkeletalMeshComponent* GetOwnerMesh() const;
	float GetCapsuleHalfHeight() const;
	bool CanBeRevivedBy(const UBH_PlayerDeathComponent* Reviver) const;

	// -- Revive input context (LOCAL player only) ---------------------------------------------------
	/** Adds / removes ABH_CharacterBase::ReviveMappingContext so it is active exactly while a downed partner can be revived by this (living, local) pawn. */
	void UpdateReviveInputContext();
	void AddReviveInputContext(UEnhancedInputLocalPlayerSubsystem* InputSubsystem);
	void RemoveReviveInputContext();

	// -- Server ---------------------------------------------------------------------------------
	void OnDeadTagChanged(const FGameplayTag Tag, int32 NewCount);
	void BeginDeath();
	void CommitState();
	void SampleBodyRest();
	bool ResolveRespawnDestination(FVector& OutLocation, float& OutYaw, FName& OutHubName, bool bLogFallback) const;
	void ReturnToLife(const FVector& Location, float Yaw, float HealthFraction);
	void SetReviveInfo(APawn* Reviver, uint8 ProgressPct);

	bool BeginReviveServer(APawn* Target);
	void CancelReviveAttempt();
	void TickRevive(float DeltaTime);
	bool ValidateReviveTarget(const UBH_PlayerDeathComponent* TargetComp) const;
	void HandleOwnerHealthChanged(const FOnAttributeChangeData& Data);

	// -- Cached / local state --------------------------------------------------------------------
	EBH_DeathPhase AppliedPhase = EBH_DeathPhase::Alive;
	uint8 AppliedEpoch = 0;
	bool bRagdolling = false;
	bool bBodyCorrected = false;
	bool bGettingUp = false;
	bool bDeathPending = false;
	bool bRootMotionOverridden = false;

	double RagdollStartTime = 0.0;
	float SlowTime = 0.f;

	/** Belly direction in the pelvis frame, recorded while still standing: decides face up / down once lying. */
	FVector BellyLocal = FVector::ZeroVector;
	bool bBellyValid = false;

	FTransform InitialSpawnTransform = FTransform::Identity;
	FTimerHandle GetUpTimer;
	FTimerHandle RootMotionRestoreTimer;
	TWeakObjectPtr<UAnimMontage> GetUpMontagePlaying;

	// Server: the dead tag subscription, and the revive attempt this pawn is making as the REVIVER.
	FDelegateHandle DeadTagHandle;
	FDelegateHandle OwnerHealthHandle;
	TWeakObjectPtr<APawn> ReviveTargetPawn;
	float ReviveElapsed = 0.f;
	bool bBoundToParty = false;

	/** Client: the reviver last reported (so a clear can still be forwarded to its machine). */
	TWeakObjectPtr<APawn> LastReportedReviver;

	// Local player: the revive mapping context currently added, and where (so it is removed from the same subsystem).
	FTimerHandle ReviveContextTimer;
	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> ReviveContextSubsystem;
	TWeakObjectPtr<UInputMappingContext> ReviveContextAsset;
	bool bReviveContextActive = false;
	bool bWarnedMissingReviveContext = false;
};
