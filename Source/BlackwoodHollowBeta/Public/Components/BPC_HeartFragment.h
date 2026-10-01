// Blackwood Hollow - Heart-Fragment ActorComponent
// Target: Unreal Engine 5.8 (C++), GAS (GameplayAbilities plugin required)
//
// Every exiled Vanguard survivor carries a crystalline Heart-Fragment
// embedded in their chest. This component is the gameplay-facing home for
// everything that artifact does:
//   1) it shields its owner against Blight damage (the Blight shield), and
//   2) it is the Heart-Fragment LOADOUT MANAGER: up to 5 equipped fragment
//      abilities (UAH_GA_FragmentBase), granted to the owner's ASC and fired by
//      slot (keys 1-5). Fragments have no mana cost; each owns a GAS cooldown.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayAbilitySpecHandle.h"
#include "BPC_HeartFragment.generated.h"

class UAbilitySystemComponent;
class UAH_AttributeSet;
class UAH_GA_FragmentBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnBlightShieldChanged, float, NewShieldValue, float, MaxShieldValue);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBlightShieldDepleted);

/**
 * UBPC_HeartFragment
 *
 * Attach to any Vanguard-lineage Pawn/Character that also has an
 * AbilitySystemComponent + UAH_AttributeSet. Ticks its own Blight shield
 * regen, exposes the entry points BP_BlightVolume hooks into, and manages the
 * Heart-Fragment ability loadout.
 */
UCLASS(ClassGroup = (BlackwoodHollow), meta = (BlueprintSpawnableComponent))
class BLACKWOODHOLLOWBETA_API UBPC_HeartFragment : public UActorComponent
{
	GENERATED_BODY()

public:
	UBPC_HeartFragment();

	/** Maximum number of equipped fragments (keys 1-5). */
	static constexpr int32 MaxFragmentSlots = 5;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

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

	// -- Mana (read-only; fragments no longer use Mana) --------------------------

	UFUNCTION(BlueprintPure, Category = "HeartFragment|Mana")
	float GetCurrentMana() const;

	UFUNCTION(BlueprintPure, Category = "HeartFragment|Mana")
	float GetMaxMana() const;

	UFUNCTION(BlueprintPure, Category = "HeartFragment|Mana")
	float GetManaPercent() const;

	// -- Fragment loadout --------------------------------------------------------

	/**
	 * Equipped fragment abilities by slot (index 0 = key 1). Max 5; null/empty slots are allowed.
	 * Granted to the owner's ASC on the server; replicated so clients can resolve slots.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "BH|Fragments")
	TArray<TSubclassOf<UAH_GA_FragmentBase>> EquippedFragments;

	/**
	 * Grants every EquippedFragments ability to the owner's ASC (server only, idempotent).
	 * Called from BeginPlay (with a short retry until the ASC is initialised) and by
	 * UBH_CombatFunctionLibrary::SetupCombatCharacter.
	 * @return true if the fragments are granted.
	 */
	UFUNCTION(BlueprintCallable, Category = "BH|Fragments")
	bool GrantEquippedFragments();

	/** Server only: replaces the fragment in Slot (0-4). Clears the old ability spec (unless another slot uses it) and grants the new one. Pass null to empty the slot. */
	UFUNCTION(BlueprintCallable, Category = "BH|Fragments")
	bool SetFragmentInSlot(int32 Slot, TSubclassOf<UAH_GA_FragmentBase> FragmentClass);

	/**
	 * Fires the fragment in Slot (0-4) on the owning client or the server; GAS handles prediction/RPCs per the
	 * ability's NetExecutionPolicy. Empty slots do nothing.
	 * @return true if activation was started (it can still be refused by the server).
	 */
	UFUNCTION(BlueprintCallable, Category = "BH|Fragments")
	bool TryActivateFragment(int32 Slot);

	/** Cooldown of the fragment in Slot: Remaining seconds (0 = ready or empty slot) and total Duration. */
	UFUNCTION(BlueprintPure, Category = "BH|Fragments")
	void GetFragmentCooldown(int32 Slot, float& Remaining, float& Duration) const;

	/** The fragment class in Slot, or null if the slot is empty / out of range. */
	UFUNCTION(BlueprintPure, Category = "BH|Fragments")
	TSubclassOf<UAH_GA_FragmentBase> GetFragmentClass(int32 Slot) const;

protected:
	UPROPERTY(Transient)
	TObjectPtr<UAbilitySystemComponent> CachedASC;

	UPROPERTY(Transient)
	TObjectPtr<const UAH_AttributeSet> CachedAttributeSet;

	/** Seconds since the shield last absorbed damage; drives the regen delay gate. */
	float TimeSinceLastShieldHit = 0.f;

	/** Resolves and caches the owner's AbilitySystemComponent + UAH_AttributeSet. Safe to call repeatedly. */
	bool EnsureAbilitySystemCached();

private:
	/** Spec handles parallel to EquippedFragments (server only; clients resolve by class). */
	TArray<FGameplayAbilitySpecHandle> FragmentHandles;

	bool bFragmentsGranted = false;
	int32 GrantRetryCount = 0;
	FTimerHandle GrantRetryTimer;

	UAbilitySystemComponent* GetOwnerASC() const;
	static bool IsASCReady(const UAbilitySystemComponent* ASC);
	void TrimFragmentsToMax();
	void RetryGrantFragments();
	FGameplayAbilitySpecHandle ResolveFragmentHandle(int32 Slot) const;
};
