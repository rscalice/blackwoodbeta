// Blackwood Hollow - Posture break (guard break / deathblow window) ability
// Target: Unreal Engine 5.8 (C++), GAS (GameplayAbilities plugin required)
//
// Triggered by Event.Combat.PostureBreak, which UAH_AttributeSet sends the
// moment Posture reaches 0 (from a posture GE or a blocked hit). While active:
//   - State.Combat.PostureBroken is held (blocks attacking, parrying, blocking
//     and hit reactions),
//   - character movement is disabled,
//   - the vulnerable stagger montage plays.
// When BreakDuration elapses, movement is restored, the tag is cleared and
// Posture is refilled (RecoveredPosturePercent of MaxPosture).

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "AH_GA_PostureBreak.generated.h"

class UAnimMontage;
class UGameplayEffect;
class UAbilityTask_PlayMontageAndWait;

UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API UAH_GA_PostureBreak : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UAH_GA_PostureBreak();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	/** The vulnerable stagger animation. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PostureBreak")
	TObjectPtr<UAnimMontage> PostureBreakMontage;

	/** Stance (GASP OverlayPose display name, e.g. "Greatsword") -> posture break montage. Falls back to PostureBreakMontage. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PostureBreak")
	TMap<FName, TObjectPtr<UAnimMontage>> StancePostureBreakMontages;

	/** Tag-keyed twin of StancePostureBreakMontages (Stance.Weapon.*); read first, the legacy map is the fallback. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PostureBreak", meta = (Categories = "Stance.Weapon"))
	TMap<FGameplayTag, TObjectPtr<UAnimMontage>> StancePostureBreakMontagesByTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PostureBreak", meta = (ClampMin = "0.1"))
	float MontagePlayRate = 1.f;

	/** How long the character stays broken (independent of the montage length). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PostureBreak", meta = (ClampMin = "0.1"))
	float BreakDuration = 2.5f;

	/** Posture restored on recovery, as a fraction of MaxPosture. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PostureBreak", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RecoveredPosturePercent = 1.f;

	/** Instant GE adding SetByCaller(Data.PostureDamage) to Posture - used (positive) to refill on recovery. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PostureBreak")
	TSubclassOf<UGameplayEffect> PostureEffectClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PostureBreak")
	bool bDisableMovement = true;

protected:
	UFUNCTION(BlueprintImplementableEvent, Category = "PostureBreak", meta = (DisplayName = "On Posture Broken"))
	void K2_OnPostureBroken(AActor* Breaker);

	UFUNCTION(BlueprintImplementableEvent, Category = "PostureBreak", meta = (DisplayName = "On Posture Recovered"))
	void K2_OnPostureRecovered();

private:
	UFUNCTION() void OnMontageInterrupted();

	void Recover();
	void SetMovementDisabled(bool bDisabled);

	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> MontageTask;

	FTimerHandle RecoverTimerHandle;
	bool bMovementDisabledByUs = false;
};
