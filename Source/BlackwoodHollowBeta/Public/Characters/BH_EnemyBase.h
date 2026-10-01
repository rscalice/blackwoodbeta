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
};
