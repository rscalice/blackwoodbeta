// Blackwood Hollow - Native enemy / combat target base
// Target: Unreal Engine 5.8 (C++), GAS (GameplayAbilities plugin required)
//
// Minimal ACharacter with a replicated AbilitySystemComponent and
// UAH_AttributeSet created as default subobjects, so it takes part in the same
// hit / parry / block / posture pipeline as the player without any Blueprint
// setup. Make a Blueprint child (e.g. BP_BH_EnemyDummy) to assign the mesh,
// anim class, DefaultAbilities and weapon loadout.
//
// Testing helpers:
//   bAutoAttack   - periodically activates AutoAttackAbility (tests your parry/block)
//   bHoldBlock    - keeps BlockAbility active (tests guard / posture break)
//   bFaceTarget   - turns to face player 0
//   bResetOnDeath - refills Health/Posture a few seconds after dying
//
// Phase 9 scaling: EnemyLevel (set it before BeginPlay: placed in the level, ExposeOnSpawn, or a deferred spawn) and
// ScalingRowPrefix pick rows of UBH_RPGSettings::EnemyScalingTable ("<Prefix>.MaxHealth / .MaxPosture / .AttackPower / .Defense /
// .XPReward"). ApplyLevelScaling runs on the server at the end of BeginPlay, AFTER the per-class Initial* values, and only touches
// stats whose row exists. On death the enemy grants its XP (curve row at its level, else XPRewardOverride) to every living player
// pawn within UBH_RPGSettings::XPShareRadius.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "GenericTeamAgentInterface.h"
#include "Combat/BH_CombatTeam.h"
#include "BH_EnemyBase.generated.h"

class UAbilitySystemComponent;
class UAH_AttributeSet;
class UGameplayAbility;
class UBH_WeaponLoadoutDataAsset;
class ABH_EnemyBase;

/** Server: this enemy's health reached zero (fires after the death hooks and the XP grant). */
DECLARE_MULTICAST_DELEGATE_TwoParams(FBH_OnEnemyDeath, ABH_EnemyBase* /*Enemy*/, AActor* /*Killer*/);

UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API ABH_EnemyBase : public ACharacter, public IAbilitySystemInterface, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	ABH_EnemyBase();

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override { return AbilitySystemComponent; }

	virtual void PossessedBy(AController* NewController) override;

	// -- IGenericTeamAgentInterface (friendly-fire filtering, future AI perception) --
	virtual FGenericTeamId GetGenericTeamId() const override { return BH_CombatTeam::ToGenericTeamId(CombatTeam); }
	virtual void SetGenericTeamId(const FGenericTeamId& NewTeamId) override { CombatTeam = BH_CombatTeam::FromGenericTeamId(NewTeamId); }
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Enemy")
	UAH_AttributeSet* GetAttributeSet() const { return AttributeSet; }

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Enemy")
	float GetHealth() const;

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Enemy")
	float GetPosture() const;

	// -- Setup ---------------------------------------------------------------

	/** Name shown on the lock-on target vitals. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BlackwoodHollow|Enemy")
	FText DisplayName;

	/** Team for friendly-fire filtering: same-team hitboxes never connect. Neutral = hittable by everyone. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BlackwoodHollow|Enemy")
	EBH_CombatTeam CombatTeam = EBH_CombatTeam::Enemies;

	/** Granted on BeginPlay (authority). Typically HitReaction, PostureBreak, Block, Parry, a melee attack. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BlackwoodHollow|Enemy")
	TArray<TSubclassOf<UGameplayAbility>> DefaultAbilities;

	/** Weapon meshes to attach on BeginPlay (all machines). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BlackwoodHollow|Enemy")
	TObjectPtr<UBH_WeaponLoadoutDataAsset> WeaponLoadouts;

	/** Loadout entry to use (an Enum_OverlayPose display name). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BlackwoodHollow|Enemy")
	FString WeaponLoadoutName = TEXT("SwordAndShield");

	// -- Level scaling (Phase 9) ---------------------------------------------

	/** Enemy level. Set before BeginPlay (placed instance, Expose on Spawn, or the wave spawner's deferred spawn). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|Enemy|Scaling", meta = (ExposeOnSpawn = true, ClampMin = "1"))
	int32 EnemyLevel = 1;

	/** Row prefix in the enemy scaling table ("Crab" -> "Crab.MaxHealth", ...). None = this enemy is not scaled by the table. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|Enemy|Scaling")
	FName ScalingRowPrefix;

	/** XP granted on death when the enemy table has no "<Prefix>.XPReward" row. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|Enemy|Scaling", meta = (ClampMin = "0"))
	int32 XPRewardOverride = 10;

	/**
	 * SERVER. Writes EnemyLevel into the Level attribute and, for every row that exists in the enemy scaling table, sets the BASE
	 * MaxHealth (refilling Health), MaxPosture (refilling Posture), AttackPower and Defense. Missing rows leave the current value alone.
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Enemy|Scaling")
	void ApplyLevelScaling();

	/** XP this enemy grants on death: the table's "<Prefix>.XPReward" at EnemyLevel, else XPRewardOverride. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Enemy|Scaling")
	int32 GetXPReward() const;

	/** True from the moment the health hits zero (cleared again if bResetOnDeath revives it). */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Enemy")
	bool IsEnemyDead() const { return bDead; }

	/** Server only: fires when this enemy dies (the wave spawner listens to it). */
	FBH_OnEnemyDeath OnEnemyDeath;

	// -- Test behaviour ------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|Enemy|Testing")
	bool bAutoAttack = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|Enemy|Testing", meta = (EditCondition = "bAutoAttack", ClampMin = "0.5"))
	float AutoAttackInterval = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|Enemy|Testing")
	TSubclassOf<UGameplayAbility> AutoAttackAbility;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|Enemy|Testing")
	bool bHoldBlock = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|Enemy|Testing")
	TSubclassOf<UGameplayAbility> BlockAbility;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|Enemy|Testing")
	bool bFaceTarget = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|Enemy|Testing", meta = (EditCondition = "bFaceTarget", ClampMin = "0.0"))
	float FaceTargetInterpSpeed = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|Enemy|Testing")
	bool bResetOnDeath = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|Enemy|Testing", meta = (EditCondition = "bResetOnDeath", ClampMin = "0.1"))
	float ResetDelay = 3.f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	 * Native death hook (authority): runs inside the Health-zero handler, after all abilities were cancelled and BEFORE
	 * K2_OnDeath. Subclasses override this to ragdoll / despawn (ABH_EnemyCrab). Note the State.Combat.Dead loose tag is
	 * added by the attribute set right AFTER this runs.
	 */
	virtual void OnDeathNative(AActor* Killer) {}

	/**
	 * Server, end of BeginPlay: writes this class's starting stats. The base applies the level scaling; subclasses that have their own
	 * Initial* values (ABH_EnemyCrab) apply those first and then call Super, so the scaling table always wins where a row exists.
	 */
	virtual void InitializeServerStats();

	/** Health reached zero (authority). */
	UFUNCTION(BlueprintImplementableEvent, Category = "BlackwoodHollow|Enemy", meta = (DisplayName = "On Death"))
	void K2_OnDeath(AActor* Killer);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BlackwoodHollow|Enemy")
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BlackwoodHollow|Enemy")
	TObjectPtr<UAH_AttributeSet> AttributeSet;

private:
	void InitAbilitySystem();
	void HandleHealthZero(AActor* Killer);
	void ResetAfterDeath();
	void DoAutoAttack();
	void StartHoldBlock();

	FTimerHandle AutoAttackTimerHandle;
	FTimerHandle ResetTimerHandle;
	FTimerHandle HoldBlockTimerHandle;
	bool bAbilitiesGranted = false;
	bool bDead = false;
};
