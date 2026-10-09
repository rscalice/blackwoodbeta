// Blackwood Hollow - Hit reaction (flinch / stagger) ability
// Target: Unreal Engine 5.8 (C++), GAS (GameplayAbilities plugin required)
//
// Grant to anything that can be hit. Triggered automatically by
// Event.Combat.DamageReceived (sent by UAH_AttributeSet after IncomingDamage
// lands on Health - i.e. NOT for parried or blocked hits). Picks a directional
// montage from the attacker's position, grants State.Combat.Staggered while it
// plays, and interrupts any running attack / parry.
//
// Phase 11E safety (a client pawn hit by a raw melee damage GE stayed Staggered + MovementLocked forever):
//   * ActivationOwnedTags (Staggered + MovementLocked) are ref-counted loose tags added once per activation and removed once per EndAbility.
//     The ability is ServerInitiated with bRetriggerInstancedAbility, so a second hit while the first reaction still plays can activate the
//     CLIENT copy twice without an EndAbility in between: two tag references, one removal, tags stuck. ActivateAbility now counts activations
//     since the last EndAbility and gives back the surplus reference immediately.
//   * MaxReactionSeconds is a hard cap on any single reaction (montage or fallback timer): if neither ever finishes, the ability ends itself.
//   * After EndAbility, the owning client scrubs any Staggered reference that is still held although no reaction is running (this ability is
//     the only source of State.Combat.Staggered) and gives the matching MovementLocked references back.

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

	/** Safety cap: no reaction (stagger + movement lock) may last longer than this, whatever the montage or the timers do. Longest authored reaction is well under 2 s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "HitReaction", meta = (ClampMin = "0.5", ForceUnits = "s"))
	float MaxReactionSeconds = 3.f;

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

	/** The MaxReactionSeconds cap fired: end the reaction (logs a warning, because it means a montage / timer never finished). */
	void OnSafetyTimeout();

	/** Owning client only: removes Staggered / MovementLocked references that outlived the last reaction. */
	void ScrubLeakedTags(const FGameplayAbilityActorInfo* ActorInfo);

	UAnimMontage* GetMontageForDirection(EBH_HitDirection Direction) const;

	/** True stance-specific lookup (nullptr when the current stance has no entry / montage). */
	UAnimMontage* GetStanceMontage(EBH_HitDirection Direction) const;

	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> MontageTask;

	FTimerHandle FallbackTimerHandle;
	FTimerHandle SafetyTimerHandle;

	/** ActivateAbility calls since the last EndAbility. More than 1 = a re-activation without an end (extra tag references to give back). */
	int32 OutstandingActivations = 0;
};
