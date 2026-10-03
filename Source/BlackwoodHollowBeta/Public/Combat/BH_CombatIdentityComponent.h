// Blackwood Hollow - combat identity for non-ABH_EnemyBase characters
// Target: Unreal Engine 5.8 (C++), GAS
//
// Drop this on any Blueprint character that should take part in the combat pipeline as an AI-driven
// combatant (e.g. a child of GASP's CBP_SandboxCharacter_Manny, which has no C++ base). It carries the data
// ABH_EnemyBase has (name, team, abilities, loadout, death/reset) and does the server-side setup:
//   Minimal ASC replication -> SetupCombatCharacter -> grant abilities -> starting stance -> health-zero handling.
// On every machine it keeps the mesh animating when off-screen (anim notifies drive hitboxes) and applies the
// optional overlay material. The team is replicated here, so GetCombatTeamId resolves on clients where AI pawns
// have no controller.
//
// The character needs an AbilitySystemComponent (added in its Blueprint) and, for weapon meshes on every machine,
// a UBH_StanceWatcherComponent (its FallbackLoadouts is filled from WeaponLoadouts below).

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Components/ActorComponent.h"
#include "Engine/TimerHandle.h"
#include "Combat/BH_CombatTeam.h"
#include "BH_CombatIdentityComponent.generated.h"

class UGameplayAbility;
class UBH_WeaponLoadoutDataAsset;
class UMaterialInterface;
class UBH_VoiceSetDataAsset;
class UAbilitySystemComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBH_OnIdentityDeath, AActor*, Killer);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FBH_OnIdentityReset);

UCLASS(ClassGroup = (BlackwoodHollow), meta = (BlueprintSpawnableComponent))
class BLACKWOODHOLLOWBETA_API UBH_CombatIdentityComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBH_CombatIdentityComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** The identity component on Actor, or null. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Combat")
	static UBH_CombatIdentityComponent* Find(const AActor* Actor);

	/** Name shown on the lock-on target vitals. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "BH|Identity")
	FText DisplayName;

	/** Team for friendly-fire filtering and AI target selection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "BH|Identity")
	EBH_CombatTeam CombatTeam = EBH_CombatTeam::Enemies;

	// -- Server-side setup data ---------------------------------------------------------

	/** Granted on BeginPlay (authority). Typically HitReaction, PostureBreak, Block, Parry, a melee attack, Dodge. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BH|Setup")
	TArray<TSubclassOf<UGameplayAbility>> DefaultAbilities;

	/** Weapon loadouts; handed to the character's UBH_StanceWatcherComponent (FallbackLoadouts) on every machine. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BH|Setup")
	TObjectPtr<UBH_WeaponLoadoutDataAsset> WeaponLoadouts;

	/** GASP overlay pose (Enum_OverlayPose display name) applied on the server shortly after BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BH|Setup")
	FName StartingStance = TEXT("SwordAndShield");

	/** Tag-keyed twin of StartingStance, applied through the native UBH_StanceComponent when set (motion-matching enemies leave both empty and use the stance component's DefaultStance). Wins over StartingStance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BH|Setup", meta = (Categories = "Stance.Weapon"))
	FGameplayTag StartingStanceTag;

	/** Seconds after BeginPlay before the starting stance is applied (GASP's own setup must have run first). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BH|Setup", meta = (ClampMin = "0.0"))
	float StartingStanceDelay = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Death")
	bool bResetOnDeath = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BH|Death", meta = (EditCondition = "bResetOnDeath", ClampMin = "0.1"))
	float ResetDelay = 4.f;

	// -- Per-archetype stats (BH|Stats) ---------------------------------------------------
	// Applied on the server right after SetupCombatCharacter (which creates the attribute set with the constructor
	// defaults: Health/MaxHealth 100, Posture 100, Stamina 100, AttackPower 10, Defense 5). A value is only used when its
	// Override flag is ticked. Max* values also fill the matching current value (Health/Posture/Stamina), and
	// ResetAfterDeath restores to these values. Tune these live in the BP class defaults of each enemy archetype.

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Stats")
	bool bOverrideMaxHealth = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Stats", meta = (EditCondition = "bOverrideMaxHealth", ClampMin = "1"))
	float MaxHealth = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Stats")
	bool bOverrideAttackPower = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Stats", meta = (EditCondition = "bOverrideAttackPower"))
	float AttackPower = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Stats")
	bool bOverrideDefense = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Stats", meta = (EditCondition = "bOverrideDefense"))
	float Defense = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Stats")
	bool bOverrideMaxPosture = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Stats", meta = (EditCondition = "bOverrideMaxPosture", ClampMin = "1"))
	float MaxPosture = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Stats")
	bool bOverrideMaxStamina = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Stats", meta = (EditCondition = "bOverrideMaxStamina", ClampMin = "1"))
	float MaxStamina = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Stats")
	bool bOverrideAttackSpeed = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Stats", meta = (EditCondition = "bOverrideAttackSpeed", ClampMin = "0.5", ClampMax = "2.0"))
	float AttackSpeed = 1.f;

	// -- Collision ----------------------------------------------------------------------

	/** Makes the capsule Block the Pawn channel on every machine (GASP's capsule profile ignores it, so pawns would pass through each other). Melee hitboxes use object-type sweeps and are unaffected. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Collision")
	bool bBlockPawns = true;

	// -- Look ---------------------------------------------------------------------------

	/** Applied as the overlay material to the character's skeletal meshes on every machine. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BH|Look")
	TObjectPtr<UMaterialInterface> OverlayMaterial;

	/** Vocal efforts / hurt / death sounds for this character (see UBH_VoiceSetDataAsset; pitch multiplier lives there). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BH|Look")
	TObjectPtr<UBH_VoiceSetDataAsset> VoiceSet;

	/** Overrides the weapon trail material chosen from stance / overlay (see UBH_WeaponTrailComponent). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BH|Look")
	TObjectPtr<UMaterialInterface> TrailMaterialOverride;

	// -- Events (server) ----------------------------------------------------------------

	/** Health reached zero (authority). */
	UPROPERTY(BlueprintAssignable, Category = "BH|Death")
	FBH_OnIdentityDeath OnDeath;

	/** Refilled and revived after ResetDelay (authority). */
	UPROPERTY(BlueprintAssignable, Category = "BH|Death")
	FBH_OnIdentityReset OnReset;

	UFUNCTION(BlueprintPure, Category = "BH|Death")
	bool IsDead() const { return bDead; }

	/** Convenience setter (the wave spawner turns the auto-reset off for its enemies). */
	UFUNCTION(BlueprintCallable, Category = "BH|Death")
	void SetResetOnDeath(bool bInResetOnDeath) { bResetOnDeath = bInResetOnDeath; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void ApplyCosmetics();
	void ApplyCollision();
	void ApplyLateSetup();
	void ApplyInitialStats(UAbilitySystemComponent* ASC) const;
	void InitServer();
	void ApplyStartingStance();
	void HandleHealthZero(AActor* Killer);
	void ResetAfterDeath();
	UAbilitySystemComponent* ResolveASC() const;

	FTimerHandle StanceTimer;
	FTimerHandle ResetTimer;
	FTimerHandle CosmeticsTimer;
	FDelegateHandle HealthZeroHandle;
	bool bDead = false;
};
