// Blackwood Hollow - Hit reaction (flinch / stagger) ability
// Target: Unreal Engine 5.8 (C++), GAS (GameplayAbilities plugin required)
//
// Grant to anything that can be hit. Triggered automatically by
// Event.Combat.DamageReceived (sent by UAH_AttributeSet after IncomingDamage
// lands on Health - i.e. NOT for parried or blocked hits). Picks a directional
// montage from the attacker's position, grants State.Combat.Staggered while it
// plays, and interrupts any running attack / parry.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "AH_GA_HitReaction.generated.h"

class UAnimMontage;
class UAbilityTask_PlayMontageAndWait;

/** Side of the victim the hit came FROM. */
UENUM(BlueprintType)
enum class EBH_HitDirection : uint8
{
	Front,
	Back,
	Left,
	Right,
};

/** One montage per hit side (a missing side falls back to Front). */
USTRUCT(BlueprintType)
struct BLACKWOODHOLLOWBETA_API FBH_HitMontageSet
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HitReaction")
	TObjectPtr<UAnimMontage> Front;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HitReaction")
	TObjectPtr<UAnimMontage> Back;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HitReaction")
	TObjectPtr<UAnimMontage> Left;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HitReaction")
	TObjectPtr<UAnimMontage> Right;
};

UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API UAH_GA_HitReaction : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UAH_GA_HitReaction();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	virtual bool ShouldAbilityRespondToEvent(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayEventData* Payload) const override;

	/** Hit from the front. Also the fallback for any direction without its own montage. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HitReaction")
	TObjectPtr<UAnimMontage> HitMontageFront;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HitReaction")
	TObjectPtr<UAnimMontage> HitMontageBack;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HitReaction")
	TObjectPtr<UAnimMontage> HitMontageLeft;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HitReaction")
	TObjectPtr<UAnimMontage> HitMontageRight;

	/** Stance (GASP OverlayPose display name, e.g. "Greatsword") -> directional hit montages. Falls back to the montages above. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HitReaction")
	TMap<FName, FBH_HitMontageSet> StanceHitMontages;

	/** Tag-keyed twin of StanceHitMontages (Stance.Weapon.*); read first, the legacy map is the fallback. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HitReaction", meta = (Categories = "Stance.Weapon"))
	TMap<FGameplayTag, FBH_HitMontageSet> StanceHitMontagesByTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HitReaction", meta = (ClampMin = "0.1"))
	float MontagePlayRate = 1.f;

	/** Hits dealing less than this don't flinch (0 = every hit reacts). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HitReaction", meta = (ClampMin = "0.0"))
	float MinimumDamageToReact = 0.f;

	/** Used when no montage is set: how long State.Combat.Staggered is held. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HitReaction", meta = (ClampMin = "0.05"))
	float FallbackStaggerDuration = 0.4f;

	/** Which side of Victim a hit from SourceLocation lands on. */
	UFUNCTION(BlueprintPure, Category = "HitReaction")
	static EBH_HitDirection ComputeHitDirection(const AActor* Victim, const FVector& SourceLocation);

protected:
	/** Fired when the reaction starts. Spawn blood / sparks / camera shake here. */
	UFUNCTION(BlueprintImplementableEvent, Category = "HitReaction", meta = (DisplayName = "On Hit Reaction"))
	void K2_OnHitReaction(EBH_HitDirection Direction, AActor* Attacker, float DamageTaken);

private:
	UFUNCTION() void OnMontageFinished();
	UFUNCTION() void OnMontageInterrupted();

	void FinishReaction();

	UAnimMontage* GetMontageForDirection(EBH_HitDirection Direction) const;

	/** True stance-specific lookup (nullptr when the current stance has no entry / montage). */
	UAnimMontage* GetStanceMontage(EBH_HitDirection Direction) const;

	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> MontageTask;

	FTimerHandle FallbackTimerHandle;
};
