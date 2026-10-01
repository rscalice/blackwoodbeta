// Blackwood Hollow - Lock-on targeting component
// Target: Unreal Engine 5.8 (C++), Enhanced Input, GAS
//
// Lives on the PlayerController. Hard lock: while a target is locked the camera (control rotation) is
// steered onto it, look input is ignored, the pawn is kept in GASP strafe mode, the target gets an
// outline (custom stencil + post process) and a ground ring decal (both local only), and the HUD's
// target vitals widget is told about it.
//
// Networking: the owning client decides and sends Server_SetLockedTarget (validated); the server mirrors
// it so server-side logic (melee facing, auto clear) sees it. LockedTarget replicates back to the owner
// only, so a server-side clear (dead / out of range) reaches the client through OnRep.
//
// Input: LockOnAction / SwitchTargetAction are bound on the controller's UEnhancedInputComponent
// (InitializeInput, retried automatically because the InputComponent may not exist at BeginPlay).
// Right-stick and mouse flicks are detected by polling the Look actions' values (no bindings needed).

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InputActionValue.h"
#include "TimerManager.h"
#include "BH_LockOnComponent.generated.h"

class AActor;
class APawn;
class APlayerController;
class UInputAction;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UPostProcessComponent;
class UDecalComponent;
class UPrimitiveComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FBH_OnLockedTargetChanged, AActor*, OldTarget, AActor*, NewTarget);

UCLASS(ClassGroup = (BlackwoodHollow), meta = (BlueprintSpawnableComponent))
class BLACKWOODHOLLOWBETA_API UBH_LockOnComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBH_LockOnComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Locks the best target in front of the camera, or releases the current lock. Local controller only. */
	UFUNCTION(BlueprintCallable, Category = "LockOn")
	void ToggleLockOn();

	/** Direction > 0 = next target to the right of the current one on screen, < 0 = to the left. */
	UFUNCTION(BlueprintCallable, Category = "LockOn")
	void SwitchTarget(float Direction);

	UFUNCTION(BlueprintCallable, Category = "LockOn")
	void ClearLockOn();

	UFUNCTION(BlueprintPure, Category = "LockOn")
	AActor* GetLockedTarget() const { return LockedTarget; }

	UFUNCTION(BlueprintPure, Category = "LockOn")
	bool IsLockedOn() const { return LockedTarget != nullptr; }

	/**
	 * Soft lock ("attack magnetism"): the best enemy near the player's movement direction (camera forward when idle),
	 * or null. Only meaningful while NOT hard-locked (returns null when hard-locked). Cached (SoftLockCacheInterval);
	 * use RefreshSoftTarget() for a fresh query. No camera control, no strafe forcing.
	 */
	UFUNCTION(BlueprintPure, Category = "LockOn|Soft")
	AActor* GetSoftTarget() const;

	/** Re-queries the soft target now (owning client AND server; the server derives the same answer from replicated movement). */
	UFUNCTION(BlueprintCallable, Category = "LockOn|Soft")
	AActor* RefreshSoftTarget();

	/**
	 * Melee facing: turns the controlled pawn's yaw toward the hard target (bAllowHard) or else a fresh soft target
	 * (bAllowSoft; skipped when it would need a turn above MaxSoftLockTurnDeg). Runs on the owning client and on the
	 * server (which derives its own soft target). bNotifyServer: a remote client also sends the chosen target to the server
	 * (Server_ApplyMeleeFacing), used for combo steps the server never sees input for. Returns true if it rotated.
	 */
	UFUNCTION(BlueprintCallable, Category = "LockOn|Soft")
	bool FaceMeleeTarget(bool bAllowHard, bool bAllowSoft, bool bNotifyServer, bool& bOutUsedSoftTarget);

	/**
	 * GASP rotates the pawn to the control (camera) yaw every tick (CMC controller desired rotation, re-enabled by its own
	 * Blueprint), which would undo a soft-lock turn on the next frame. While a soft-facing swing plays, this component holds the
	 * pawn's yaw at Yaw every tick (post-physics) without touching the camera. EndMeleeFacingOverride (called by the ability
	 * when it ends) eases the pawn back to the camera yaw at SoftLockRecoverTurnRate. Safe to call repeatedly.
	 */
	void BeginMeleeFacingOverride(float Yaw);

	UFUNCTION(BlueprintCallable, Category = "LockOn|Soft")
	void EndMeleeFacingOverride();

	/** Binds LockOnAction / SwitchTargetAction on the controller's Enhanced Input component (idempotent). */
	UFUNCTION(BlueprintCallable, Category = "LockOn")
	void InitializeInput();

	/** Fires on every machine where LockedTarget changes (owning client + server). */
	UPROPERTY(BlueprintAssignable, Category = "LockOn")
	FBH_OnLockedTargetChanged OnLockedTargetChanged;

	// -- Tunables -----------------------------------------------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn")
	float AcquireRange = 1500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn")
	float BreakRange = 2000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn")
	float AcquireHalfAngleDeg = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn")
	float CameraYawInterpSpeed = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn")
	float CameraPitchInterpSpeed = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn")
	float PitchMin = -35.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn")
	float PitchMax = 15.f;

	/** Added to the look-at pitch so the target sits a little above screen centre. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn")
	float PitchBias = -8.f;

	/** 0 = feet, 1 = head (of the capsule). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn")
	float TargetHeightFraction = 0.55f;

	/** Right-stick X magnitude that counts as a switch flick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn")
	float FlickThreshold = 0.75f;

	/** Mouse X delta per frame (Look action units) that counts as a switch flick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn")
	float MouseFlickThreshold = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn")
	float FlickCooldown = 0.35f;

	/** Seconds the target may stay out of line of sight before the lock breaks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn")
	float LoseLineOfSightGrace = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Input")
	TObjectPtr<UInputAction> LockOnAction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Input")
	TObjectPtr<UInputAction> SwitchTargetAction;

	/** Mouse look action (Axis2D) - polled for mouse flicks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Input")
	TObjectPtr<UInputAction> LookAction;

	/** Gamepad look action (Axis2D) - polled for right-stick flicks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Input")
	TObjectPtr<UInputAction> LookGamepadAction;

	/** Post Process domain material that outlines stencil == TargetStencilValue. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Visuals")
	TObjectPtr<UMaterialInterface> OutlinePostProcessMaterial;

	/** Deferred decal material for the ring under the target (needs a scalar "Opacity"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Visuals")
	TObjectPtr<UMaterialInterface> GroundRingMaterial;

	/** Decal half extents (X = projection depth). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Visuals")
	FVector GroundRingDecalSize = FVector(64.f, 110.f, 110.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Visuals")
	int32 TargetStencilValue = 1;

	/** Seconds for the ring to fade in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Visuals")
	float GroundRingFadeTime = 0.15f;

	// -- Soft lock tunables -------------------------------------------------------------------------------------

	/** Max distance (cm) for a soft-lock candidate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Soft", meta = (ClampMin = "0.0"))
	float SoftLockRange = 400.f;

	/** Half angle (deg) of the soft-lock cone, centred on the movement input direction (camera yaw when idle). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Soft", meta = (ClampMin = "1.0", ClampMax = "180.0"))
	float SoftLockHalfAngleDeg = 70.f;

	/** Melee facing never turns the avatar by more than this to face a soft target (larger = no turn at all). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Soft", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float MaxSoftLockTurnDeg = 90.f;

	/** Seconds the cached soft target stays valid (also the local indicator refresh period). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Soft", meta = (ClampMin = "0.02"))
	float SoftLockCacheInterval = 0.1f;

	/** Deg/s the pawn turns back to the camera yaw after a soft-facing swing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Soft", meta = (ClampMin = "30.0"))
	float SoftLockRecoverTurnRate = 540.f;

	/** Safety net: the facing override ends by itself after this many seconds even if the ability never reports its end. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Soft", meta = (ClampMin = "0.5"))
	float SoftLockMaxOverrideTime = 2.5f;

	/** Local-only faint ground ring under the soft target (reuses GroundRingMaterial). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Soft")
	bool bShowSoftLockIndicator = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Soft", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SoftLockIndicatorOpacity = 0.35f;

	/** Soft indicator decal size as a fraction of GroundRingDecalSize. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LockOn|Soft", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float SoftLockIndicatorScale = 0.6f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(Server, Reliable)
	void Server_SetLockedTarget(AActor* NewTarget);

	UFUNCTION(Client, Reliable)
	void Client_LockRejected(AActor* RejectedTarget);

	/** Combo-step facing from a remote client: the server turns its pawn toward Target (validated). */
	UFUNCTION(Server, Reliable)
	void Server_ApplyMeleeFacing(AActor* Target);

	UFUNCTION()
	void OnRep_LockedTarget(AActor* OldTarget);

	UPROPERTY(ReplicatedUsing = OnRep_LockedTarget, Transient)
	TObjectPtr<AActor> LockedTarget;

private:
	APlayerController* GetPC() const;
	bool IsLocalPC() const;

	void TryBindInput();
	void OnLockOnInput(const FInputActionValue& Value);
	void OnSwitchTargetInput(const FInputActionValue& Value);

	/** Changes the lock. bNotifyServer: a client tells the server (local input path). */
	void SetLockedTargetInternal(AActor* NewTarget, bool bNotifyServer);

	// Target rules
	bool IsTargetDead(const AActor* Target) const;
	bool IsTargetAcquirable(const AActor* Target) const;
	FVector GetAimPoint(const AActor* Target) const;
	float GetTargetHalfHeight(const AActor* Target) const;
	FVector GetViewOrigin() const;
	bool HasLineOfSight(const AActor* Target) const;
	AActor* FindBestTarget() const;
	AActor* FindSwitchTarget(float Direction) const;
	bool ValidateLock(float DeltaTime);

	// Local (cosmetic / camera) state
	void SyncLocalState();
	void EnterLocalLock();
	void ExitLocalLock();
	void AttachTargetCosmetics(AActor* Target);
	void DetachTargetCosmetics();
	void UpdateCamera(float DeltaTime);
	void PollFlick(float DeltaTime);

	// GASP strafe reflection
	void ForceStrafe();
	void RestoreStrafe();

	TWeakObjectPtr<AActor> AppliedTarget;
	TWeakObjectPtr<APawn> LockPawn;
	TWeakObjectPtr<APawn> StrafePawn;
	bool bLocalLockActive = false;
	bool bIgnoringLook = false;
	bool bStrafeWasChanged = false;
	bool bInputBound = false;
	int32 InputBindRetries = 0;
	FTimerHandle InputRetryHandle;

	float LineOfSightLostTime = 0.f;
	float FlickTimer = 0.f;
	bool bGamepadFlickHeld = false;
	bool bMouseFlickHeld = false;

	struct FOutlinedComp
	{
		TWeakObjectPtr<UPrimitiveComponent> Comp;
		bool bPrevRenderCustomDepth = false;
		int32 PrevStencil = 0;
		bool bPrevReceivesDecals = true;
	};
	TArray<FOutlinedComp> OutlinedComps;

	UPROPERTY(Transient)
	TObjectPtr<UPostProcessComponent> OutlinePPComp;

	UPROPERTY(Transient)
	TObjectPtr<UDecalComponent> RingDecal;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> RingMID;

	float RingFade = 0.f;

	// Soft lock
	AActor* FindSoftTarget() const;
	void SoftLockTimerTick();
	void UpdateSoftIndicator(AActor* Target);
	void DestroySoftIndicator();

	TWeakObjectPtr<AActor> SoftTarget;
	double SoftTargetTime = -1000.0;
	FTimerHandle SoftLockTimer;
	TWeakObjectPtr<AActor> SoftIndicatorTarget;

	// Soft-facing override (see BeginMeleeFacingOverride)
	void UpdateFacingHold(float DeltaTime);
	bool bFacingOverrideActive = false;
	bool bRecoveringYaw = false;
	float HeldYaw = 0.f;
	FTimerHandle OverrideTimeoutTimer;

	UPROPERTY(Transient)
	TObjectPtr<UDecalComponent> SoftDecal;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SoftMID;
};
