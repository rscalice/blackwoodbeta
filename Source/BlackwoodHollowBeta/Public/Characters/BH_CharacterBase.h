// Blackwood Hollow - native base for the motion-matching character (GASP CMC sandbox character reparents onto this)
//
// Owns the AbilitySystemComponent + UAH_AttributeSet (on the pawn, like ABH_EnemyBase), the replicated weapon
// stance component, replicated lock-on strafe and a gait mirror for native consumers.
// The constructor only creates subobjects: every mesh / movement default stays on the Blueprint CDO.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "GenericTeamAgentInterface.h"
#include "GameplayTagContainer.h"
#include "Combat/BH_CombatTeam.h"
#include "Characters/BH_CharacterTypes.h"
#include "BH_CharacterBase.generated.h"

class UAbilitySystemComponent;
class UAH_AttributeSet;
class UBH_StanceComponent;
class UBH_StanceMovementProfile;
class UGameplayAbility;

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

	/** True while the ASC carries State.Combat.MovementLocked: profile speeds / acceleration read 0 and braking is CombatLockBrakingDeceleration. Root motion is unaffected. */
	UFUNCTION(BlueprintPure, Category = "BH|Movement")
	bool IsCombatMovementLocked() const;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BH|Movement")
	float CombatLockBrakingDeceleration = 2000.f;

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

protected:
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;
	virtual void BeginPlay() override;
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

private:
	/** Idempotent. */
	void InitAbilitySystem();
	void GrantDefaultAbilities();

	UPROPERTY(Transient, VisibleInstanceOnly, Category = "BH|Movement")
	EBH_Gait CurrentGait = EBH_Gait::Run;

	bool bAbilitiesGranted = false;

	/** Speed band latched on the first no-input tick of a stop (Gait flips on release, so it cannot be used). */
	TOptional<EBH_Gait> BrakingBand;
};
