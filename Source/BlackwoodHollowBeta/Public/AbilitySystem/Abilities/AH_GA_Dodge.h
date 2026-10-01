// Blackwood Hollow - Dodge ability (souls-style roll with i-frames)
// Target: Unreal Engine 5.8 (C++), GAS
//
// Press -> picks F / B / L / R from the held movement input (relative to the character's facing; no input = backstep),
// plays the stance's directional montage (root motion), grants State.Combat.Dodging for the whole roll and
// State.Combat.Invulnerable between IFrameStart and IFrameEnd. Costs stamina (default 20, full cost required),
// interrupts swings / guard / parry, and has no cooldown: stamina and the Dodging tag are the limiters.
//
// Author the BP child GA_Dodge and fill DirectionalMontages, keyed by the GASP OverlayPose display name
// ("SwordAndShield", "DualSword", ...). Drive it with UBH_CombatFunctionLibrary::HandleDodgeInput (tag Ability.Combat.Dodge).
//
// Networking: LocalPredicted. The owning client picks the direction from its own input; the server (which never
// sees the input vector) falls back to the replicated movement acceleration, so a direction change inside the
// last frame before the press can differ by one montage on the server (the root-motion correction then snaps it).

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/AH_GA_StaminaBase.h"
#include "TimerManager.h"
#include "AH_GA_Dodge.generated.h"

class UAnimMontage;
class UAbilityTask_PlayMontageAndWait;
class ACharacter;

UENUM(BlueprintType)
enum class EBH_DodgeDirection : uint8
{
	Forward,
	Back,
	Left,
	Right
};

/** One montage per dodge direction (Back is also the no-input backstep). Any entry may be empty: it falls back to the others. */
USTRUCT(BlueprintType)
struct BLACKWOODHOLLOWBETA_API FBH_DodgeMontageSet
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dodge")
	TObjectPtr<UAnimMontage> Forward;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dodge")
	TObjectPtr<UAnimMontage> Back;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dodge")
	TObjectPtr<UAnimMontage> Left;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dodge")
	TObjectPtr<UAnimMontage> Right;

	UAnimMontage* Get(EBH_DodgeDirection Direction) const;
};

UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API UAH_GA_Dodge : public UAH_GA_StaminaBase
{
	GENERATED_BODY()

public:
	UAH_GA_Dodge();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	/** Stance (GASP OverlayPose display name, e.g. "SwordAndShield" / "DualSword") -> directional montages. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dodge|Montages")
	TMap<FName, FBH_DodgeMontageSet> DirectionalMontages;

	/** Used when the current stance has no entry in DirectionalMontages. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dodge|Montages")
	FBH_DodgeMontageSet DefaultMontages;

	/** Montage play rate. The montage (and its root-motion travel time) shortens by this factor. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dodge|Montages", meta = (ClampMin = "0.1"))
	float DodgePlayRate = 1.2f;

	/**
	 * Multiplier on the montage's root-motion translation while the dodge plays (restored to 1.0 when it ends or is
	 * cancelled). Applied on the server and the owning client, which both run this ability. Tunes dodge distance.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dodge|Montages", meta = (ClampMin = "0.05"))
	float RootMotionTranslationScale = 0.82f;

	/** Authored at play rate 1.0 (montage seconds); the timers divide by DodgePlayRate. Seconds after activation when State.Combat.Invulnerable is granted. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dodge|IFrames", meta = (ClampMin = "0.0"))
	float IFrameStart = 0.03f;

	/** Authored at play rate 1.0 (montage seconds); the timers divide by DodgePlayRate. Seconds after activation when State.Combat.Invulnerable is removed again. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dodge|IFrames", meta = (ClampMin = "0.0"))
	float IFrameEnd = 0.28f;

	/** Movement input shorter than this (0..1) counts as "no input" (-> backstep). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Dodge|Direction", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float InputDeadZone = 0.1f;

	/**
	 * F / B / L / R for Character from its last movement input (last consumed input, else pending input, else the
	 * movement component's acceleration) relative to its facing. No input -> Back.
	 */
	UFUNCTION(BlueprintPure, Category = "Dodge")
	static EBH_DodgeDirection ResolveDodgeDirection(const ACharacter* Character, float DeadZone = 0.1f);

private:
	UFUNCTION() void OnMontageFinished();
	UFUNCTION() void OnMontageInterrupted();

	void BeginIFrames();
	void EndIFrames();

	UAnimMontage* PickMontage(const AActor* Avatar, EBH_DodgeDirection Direction) const;

	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait> MontageTask;

	FTimerHandle IFrameStartTimer;
	FTimerHandle IFrameEndTimer;
	bool bIFramesActive = false;
	TWeakObjectPtr<ACharacter> ScaledCharacter;
};
