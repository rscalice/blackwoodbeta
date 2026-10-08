// Blackwood Hollow - Heart-Fragment ActorComponent
// Target: Unreal Engine 5.8 (C++), GAS (GameplayAbilities plugin required)
//
// Every exiled Vanguard survivor carries a crystalline Heart-Fragment
// embedded in their chest. This component is the gameplay-facing home for
// everything that artifact does:
//   1) Blight SHIELDING: a percentage cut of every Blight build-up (ShieldingLevel -> ShieldingByLevel), and
//   2) the Blight BUILD-UP METER (Phase 10B), server-authoritative, and
//   3) the Heart-Fragment LOADOUT MANAGER: up to 5 equipped fragment
//      abilities (UAH_GA_FragmentBase), granted to the owner's ASC and fired by
//      slot (keys 1-3). Phase 8B: the slot model lives on ABH_PlayerState; this component mirrors it. Fragments have no resource cost; each owns a GAS cooldown.
//
// BLIGHT METER (why it lives here): the Heart-Fragment is already on every Vanguard pawn, it owns the shielding level and the
// Aegis hook the mitigation formula needs, and no new component has to be added to the player Blueprint. Actors without a
// Heart-Fragment (enemies) simply cannot build up Blight. The value itself is the replicated UAH_AttributeSet::BlightBuildup
// attribute (0..100, owner-only), written only here, on the authority.
//   * Every source calls AddBlightBuildup(Raw, Instigator): BP_BlightVolume (per second while inside), the crab's heavy pinch
//     (+35 per hit), future hazards. Mitigation: Raw * 100 / (100 + BlightResistance) * (1 - shielding) * (1 - Aegis).
//   * Decay: BlightDecayPerSecond after BlightDecayDelay seconds without build-up. It runs on a looping timer that exists only
//     while the meter is above zero (nothing ticks while idle; this component no longer ticks at all).
//   * Saturation (meter >= 100): saturation damage (15% MaxHealth) through UAH_GE_BlightSaturationDamage (the normal
//     IncomingDamage -> Health -> death path), a brief stagger (Event.Combat.DamageReceived -> UAH_GA_HitReaction), then Blight Rot
//     (UAH_GE_BlightRot: 10 s, 2.5% MaxHealth per second, -25% passive stamina regen, tag State.Status.BlightRot), then the meter
//     resets to 0.
//   * ResetBlight() (server) zeroes the meter and removes Blight Rot: for the death / revive flow.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayAbilitySpecHandle.h"
#include "TimerManager.h"
#include "BPC_HeartFragment.generated.h"

class UAbilitySystemComponent;
class UAH_AttributeSet;
class UAH_GA_FragmentBase;

/** Owning client: the shielding level changed (also fires on the server when SetShieldingLevel runs). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnShieldingLevelChanged, int32, NewLevel, float, NewFraction);

/** Server: the Blight meter saturated. SaturationDamage = health removed, bSurvived = the owner was still alive (Blight Rot was applied). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnBlightSaturated, float, SaturationDamage, bool, bSurvived);

/**
 * UBPC_HeartFragment
 *
 * Attach to any Vanguard-lineage Pawn/Character that also has an
 * AbilitySystemComponent + UAH_AttributeSet. Owns the Blight shielding level and
 * the Blight build-up meter logic, and manages the Heart-Fragment ability loadout.
 */
UCLASS(ClassGroup = (BlackwoodHollow), meta = (BlueprintSpawnableComponent))
class BLACKWOODHOLLOWBETA_API UBPC_HeartFragment : public UActorComponent
{
	GENERATED_BODY()

public:
	UBPC_HeartFragment();

	/** Maximum number of equipped fragments (keys 1-3; Phase 8B: three category-restricted slots). */
	static constexpr int32 MaxFragmentSlots = 3;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	// -- Blight shielding (Phase 10B) ----------------------------------------

	/**
	 * Index into ShieldingByLevel. Level 0 is the base shielding every Vanguard has; upgrades raise it (server:
	 * SetShieldingLevel). Replicated to the owning client.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, ReplicatedUsing = OnRep_ShieldingLevel, Category = "HeartFragment|Shielding")
	int32 ShieldingLevel = 0;

	/** Fraction of every Blight build-up removed per shielding level (0.15 = 15%). Index = ShieldingLevel; the last entry applies beyond the end. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HeartFragment|Shielding")
	TArray<float> ShieldingByLevel = { 0.15f };

	/** Fraction (0..1) of Blight build-up currently shielded: ShieldingByLevel[ShieldingLevel]. */
	UFUNCTION(BlueprintPure, Category = "HeartFragment|Shielding")
	float GetShieldingFraction() const;

	/** Server only: sets the shielding level (clamped to >= 0) and fires OnShieldingLevelChanged. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "HeartFragment|Shielding")
	void SetShieldingLevel(int32 NewLevel);

	UPROPERTY(BlueprintAssignable, Category = "HeartFragment|Shielding")
	FOnShieldingLevelChanged OnShieldingLevelChanged;

	/**
	 * Aegis hook (Phase 10B placeholder): fraction (0..1) of Blight build-up the Aegis blocks. Returns 0 until Aegis exists; override
	 * it in Blueprint or C++ (read a status tag / attribute there). Multiplies in as (1 - Aegis) after the shielding.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "HeartFragment|Shielding")
	float GetAegisReduction() const;
	virtual float GetAegisReduction_Implementation() const { return 0.f; }

	// -- Blight meter tunables (Phase 10B; locked balance values) ------------------

	/** Seconds without any build-up received before the meter starts to decay. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HeartFragment|BlightMeter", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float BlightDecayDelay = 2.f;

	/** Meter points lost per second while decaying. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HeartFragment|BlightMeter", meta = (ClampMin = "0.0"))
	float BlightDecayPerSecond = 12.f;

	/** Seconds between decay updates (the timer only runs while the meter is above zero). Also the replication rate of the decaying value. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HeartFragment|BlightMeter", meta = (ClampMin = "0.05", ForceUnits = "s"))
	float BlightDecayTickInterval = 0.2f;

	/** Saturation damage as a fraction of MaxHealth (0.15 = 15%), dealt through UAH_GE_BlightSaturationDamage. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HeartFragment|BlightMeter", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BlightSaturationDamagePercent = 0.15f;

	/** Stagger the owner (Event.Combat.DamageReceived -> UAH_GA_HitReaction) when the meter saturates. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HeartFragment|BlightMeter")
	bool bStaggerOnSaturation = true;

	/** Blight Rot duration in seconds. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HeartFragment|BlightMeter|Rot", meta = (ClampMin = "0.1", ForceUnits = "s"))
	float BlightRotDuration = 10.f;

	/** Blight Rot damage per 1 s tick as a fraction of MaxHealth (0.025 = 2.5%). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HeartFragment|BlightMeter|Rot", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BlightRotTickPercent = 0.025f;

	/** Fraction of passive stamina regen lost while Blight Rot is active (0.25 = -25%). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "HeartFragment|BlightMeter|Rot", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BlightRotStaminaRegenPenalty = 0.25f;

	// -- Blight meter API ---------------------------------------------------------

	/**
	 * SERVER. Adds Blight build-up from any source. RawAmount is mitigated here: Raw * 100 / (100 + BlightResistance) * (1 - shielding)
	 * * (1 - Aegis). Restarts the decay delay, starts the decay timer, and saturates the meter at 100 (see the file header).
	 * Ignored while the owner is dead or already mid-saturation.
	 * @param RawAmount       build-up before mitigation (volumes pass BuildupPerSecond * interval, the crab pinch 35).
	 * @param InstigatorActor the source actor (volume, attacker); used for kill credit / stagger direction. May be null.
	 * @return the mitigated amount actually added (0 if ignored or fully shielded).
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "HeartFragment|BlightMeter")
	float AddBlightBuildup(float RawAmount, AActor* InstigatorActor);

	/** The mitigation formula alone (no state change): Raw * 100 / (100 + BlightResistance) * (1 - shielding) * (1 - Aegis). */
	UFUNCTION(BlueprintPure, Category = "HeartFragment|BlightMeter")
	float ComputeMitigatedBuildup(float RawAmount) const;

	/** Current meter value 0..100 (valid on the server and the owning client). */
	UFUNCTION(BlueprintPure, Category = "HeartFragment|BlightMeter")
	float GetBlightBuildup() const;

	/** HUD stack count: floor(meter / 10), 0..10. */
	UFUNCTION(BlueprintPure, Category = "HeartFragment|BlightMeter")
	int32 GetBlightStacks() const;

	/** SERVER. Meter to 0 and Blight Rot removed (death / revive flow). Safe to call any time. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "HeartFragment|BlightMeter")
	void ResetBlight();

	/** Server: the meter saturated (after the damage, stagger and Rot, before the reset is visible). Cosmetic cues / sound hook in here. */
	UPROPERTY(BlueprintAssignable, Category = "HeartFragment|BlightMeter")
	FOnBlightSaturated OnBlightSaturated;

	// -- Fragment loadout --------------------------------------------------------

	/**
	 * Equipped fragment abilities by slot (index 0 = key 1). Max 3; null/empty slots are allowed.
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

	/** Server only: replaces the fragment in Slot (0-2). Clears the old ability spec (unless another slot uses it) and grants the new one. Pass null to empty the slot. */
	UFUNCTION(BlueprintCallable, Category = "BH|Fragments")
	bool SetFragmentInSlot(int32 Slot, TSubclassOf<UAH_GA_FragmentBase> FragmentClass);

	/**
	 * Phase 8B (server only): mirrors ABH_PlayerState's replicated slot model onto this component, granting / clearing the
	 * abilities. Called from BeginPlay (retried until the ASC and PlayerState exist) and by ABH_PlayerState whenever a slot changes.
	 * @return true if the pawn's PlayerState was found and the slots were applied.
	 */
	UFUNCTION(BlueprintCallable, Category = "BH|Fragments")
	bool SyncFromPlayerState();

	/**
	 * Fires the fragment in Slot (0-2) on the owning client or the server; GAS handles prediction/RPCs per the
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

	/** Resolves and caches the owner's AbilitySystemComponent + UAH_AttributeSet. Safe to call repeatedly. */
	bool EnsureAbilitySystemCached();

private:
	/** Owning client: broadcasts OnShieldingLevelChanged. */
	UFUNCTION()
	void OnRep_ShieldingLevel(int32 OldLevel);

	// -- Blight meter internals (server) --
	/** Writes the BlightBuildup attribute base (clamped 0..100 by the attribute set). */
	void SetBlightBuildupValue(float NewValue);
	/** The owner's UAH_AttributeSet: the cache if filled, else looked up (const-safe; the cache is filled by EnsureAbilitySystemCached). */
	const UAH_AttributeSet* ResolveAttributeSet() const;
	void EnsureBlightDecayTimer();
	void StopBlightDecayTimer();
	void BlightDecayTick();
	/** Meter reached 100: damage, stagger, Blight Rot, reset (in that order). */
	void SaturateBlightMeter(AActor* InstigatorActor);

	FTimerHandle BlightDecayTimer;
	double LastBlightGainTime = -1.0e9;
	double LastBlightDecayTime = -1.0e9;
	bool bSaturating = false;

	/** Spec handles parallel to EquippedFragments (server only; clients resolve by class). */
	TArray<FGameplayAbilitySpecHandle> FragmentHandles;

	bool bFragmentsGranted = false;
	bool bSyncedFromPlayerState = false;
	int32 PlayerStateSyncRetryCount = 0;
	FTimerHandle PlayerStateSyncTimer;
	void RetrySyncFromPlayerState();
	int32 GrantRetryCount = 0;
	FTimerHandle GrantRetryTimer;

	UAbilitySystemComponent* GetOwnerASC() const;
	static bool IsASCReady(const UAbilitySystemComponent* ASC);
	void TrimFragmentsToMax();
	void RetryGrantFragments();
	FGameplayAbilitySpecHandle ResolveFragmentHandle(int32 Slot) const;
};
