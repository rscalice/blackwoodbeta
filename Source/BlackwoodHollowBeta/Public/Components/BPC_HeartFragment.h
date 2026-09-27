// Blackwood Hollow - Heart-Fragment ActorComponent
// Target: Unreal Engine 5.8 (C++), GAS (GameplayAbilities plugin required)
//
// Every exiled Vanguard survivor carries a crystalline Heart-Fragment
// embedded in their chest. This component is the gameplay-facing home for
// everything that artifact does: it shields its owner against Blight
// damage, taps into the Mana pool on UAH_AttributeSet for regen/hooks, and
// gates + fires the Overload Burst (GA_HeartFragment_OverloadBurst).

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BPC_HeartFragment.generated.h"

class UAbilitySystemComponent;
class UAH_AttributeSet;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnBlightShieldChanged, float, NewShieldValue, float, MaxShieldValue);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBlightShieldDepleted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnOverloadBurstTriggered);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnOverloadBurstReady);

/**
 * UBPC_HeartFragment
 *
 * Attach to any Vanguard-lineage Pawn/Character that also has an
 * AbilitySystemComponent + UAH_AttributeSet. Ticks its own Blight shield
 * regen and, optionally, passive Mana regen; exposes the entry points
 * BP_BlightVolume and combat abilities hook into.
 */
UCLASS(ClassGroup = (BlackwoodHollow), meta = (BlueprintSpawnableComponent))
class BLACKWOODHOLLOWBETA_API UBPC_HeartFragment : public UActorComponent
{
	GENERATED_BODY()

public:
	UBPC_HeartFragment();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;

public:
	// -- Blight shielding ---------------------------------------------------

	/** Maximum Blight shield the Heart-Fragment can hold, before BlightResistance scaling. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HeartFragment|BlightShield")
	float MaxBlightShield = 50.f;

	/** Shield regenerated per second once RegenDelay has elapsed since last absorbing damage. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HeartFragment|BlightShield")
	float BlightShieldRegenPerSecond = 4.f;

	/** Seconds after the last absorbed hit before shield regen resumes. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HeartFragment|BlightShield")
	float BlightShieldRegenDelay = 3.f;

	/** Current Blight shield value; absorbs BP_BlightVolume / Blight fog damage before it reaches Health. */
	UPROPERTY(BlueprintReadOnly, Category = "HeartFragment|BlightShield")
	float CurrentBlightShield = 0.f;

	/**
	 * Applies incoming Blight damage to the shield first, letting overflow
	 * through. Called by BP_BlightVolume's overlap/tick logic.
	 * @return the portion of BlightDamage NOT absorbed by the shield (i.e. what should still hit Health).
	 */
	UFUNCTION(BlueprintCallable, Category = "HeartFragment|BlightShield")
	float AbsorbBlightDamage(float BlightDamage);

	/** Instantly restores the Blight shield to MaxBlightShield (e.g. on Overload Burst, or a pickup). */
	UFUNCTION(BlueprintCallable, Category = "HeartFragment|BlightShield")
	void RechargeBlightShield();

	UFUNCTION(BlueprintPure, Category = "HeartFragment|BlightShield")
	float GetBlightShieldPercent() const { return MaxBlightShield > 0.f ? CurrentBlightShield / MaxBlightShield : 0.f; }

	UPROPERTY(BlueprintAssignable, Category = "HeartFragment|BlightShield")
	FOnBlightShieldChanged OnBlightShieldChanged;

	UPROPERTY(BlueprintAssignable, Category = "HeartFragment|BlightShield")
	FOnBlightShieldDepleted OnBlightShieldDepleted;

	// -- Mana pool hooks ------------------------------------------------------

	/** Passive mana regen per second applied by this component (in addition to any GameplayEffect-driven regen). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HeartFragment|Mana")
	float PassiveManaRegenPerSecond = 2.f;

	/** If false, this component does not drive passive mana regen itself (e.g. a GE_ManaRegen effect owns it instead). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HeartFragment|Mana")
	bool bDrivePassiveManaRegen = true;

	UFUNCTION(BlueprintPure, Category = "HeartFragment|Mana")
	float GetCurrentMana() const;

	UFUNCTION(BlueprintPure, Category = "HeartFragment|Mana")
	float GetMaxMana() const;

	UFUNCTION(BlueprintPure, Category = "HeartFragment|Mana")
	float GetManaPercent() const;

	/** Adds (or subtracts, with a negative value) Mana directly on the owner's UAH_AttributeSet. */
	UFUNCTION(BlueprintCallable, Category = "HeartFragment|Mana")
	void ModifyMana(float Delta);

	/** True if there is at least RequiredMana available -- gate check used before spending abilities. */
	UFUNCTION(BlueprintPure, Category = "HeartFragment|Mana")
	bool HasEnoughMana(float RequiredMana) const;

	// -- Overload Burst -------------------------------------------------------

	/** Mana cost to trigger the Overload Burst. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HeartFragment|OverloadBurst")
	float OverloadBurstManaCost = 40.f;

	/** Cooldown (seconds) between Overload Burst activations, enforced locally in addition to any GE cooldown on the ability itself. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HeartFragment|OverloadBurst")
	float OverloadBurstCooldown = 20.f;

	UPROPERTY(BlueprintReadOnly, Category = "HeartFragment|OverloadBurst")
	float OverloadBurstCooldownRemaining = 0.f;

	UFUNCTION(BlueprintPure, Category = "HeartFragment|OverloadBurst")
	bool IsOverloadBurstReady() const { return OverloadBurstCooldownRemaining <= 0.f; }

	/**
	 * Attempts to trigger the Overload Burst: checks mana + cooldown, spends
	 * mana, starts the cooldown, and sends Event.Combat.OverloadBurst to the
	 * owner's AbilitySystemComponent so GA_HeartFragment_OverloadBurst (bound
	 * to that tag as its trigger) activates. BP_BlightVolume listens for the
	 * resulting state/event to react (e.g. temporarily clearing its fog).
	 * @return true if the burst was successfully triggered.
	 */
	UFUNCTION(BlueprintCallable, Category = "HeartFragment|OverloadBurst")
	bool TryActivateOverloadBurst();

	UPROPERTY(BlueprintAssignable, Category = "HeartFragment|OverloadBurst")
	FOnOverloadBurstTriggered OnOverloadBurstTriggered;

	UPROPERTY(BlueprintAssignable, Category = "HeartFragment|OverloadBurst")
	FOnOverloadBurstReady OnOverloadBurstReady;

protected:
	UPROPERTY(Transient)
	TObjectPtr<UAbilitySystemComponent> CachedASC;

	UPROPERTY(Transient)
	TObjectPtr<const UAH_AttributeSet> CachedAttributeSet;

	/** Seconds since the shield last absorbed damage; drives the regen delay gate. */
	float TimeSinceLastShieldHit = 0.f;

	/** Resolves and caches the owner's AbilitySystemComponent + UAH_AttributeSet. Safe to call repeatedly. */
	bool EnsureAbilitySystemCached();
};
