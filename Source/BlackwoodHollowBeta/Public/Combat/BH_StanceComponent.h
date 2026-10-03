// Blackwood Hollow - replicated weapon stance (tag keyed) for ABH_CharacterBase descendants
//
// Source of truth is CurrentStance (Stance.Weapon.*). It is mirrored as a loose tag on the owner's ASC on every
// machine, attaches the weapon meshes from WeaponLoadouts and notifies the HUD. Never put this on an actor that also
// has UBH_StanceWatcherComponent (GASPALS path): the watcher would re-equip.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "TimerManager.h"
#include "BH_StanceComponent.generated.h"

class UAbilitySystemComponent;
class UBH_StanceMovementProfile;
class UBH_WeaponLoadoutDataAsset;
class UAnimMontage;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FBH_OnStanceChanged, FGameplayTag, OldStance, FGameplayTag, NewStance);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBH_OnWeaponDrawnChanged, bool, bDrawn);

/** Draw / sheath transition montages of one stance (UpperBody slot, played locally on every machine). Either may be null. */
USTRUCT(BlueprintType)
struct FBH_WeaponStateMontages
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Stance")
	TObjectPtr<UAnimMontage> Draw = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Stance")
	TObjectPtr<UAnimMontage> Sheath = nullptr;
};

UCLASS(ClassGroup = (BlackwoodHollow), meta = (BlueprintSpawnableComponent))
class BLACKWOODHOLLOWBETA_API UBH_StanceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBH_StanceComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Stance applied on the server at BeginPlay when nothing set one earlier. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BH|Stance", meta = (Categories = "Stance.Weapon"))
	FGameplayTag DefaultStance;

	/** Weapon meshes per stance (legacy-keyed loadout asset). Attached on every machine on stance change. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BH|Stance")
	TObjectPtr<UBH_WeaponLoadoutDataAsset> WeaponLoadouts;

	/** Movement profile per weapon stance (exact tag match). Missing stance -> DefaultMovementProfile -> nullptr (BP values). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BH|Movement", meta = (Categories = "Stance.Weapon"))
	TMap<FGameplayTag, TObjectPtr<UBH_StanceMovementProfile>> MovementProfiles;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BH|Movement")
	TObjectPtr<UBH_StanceMovementProfile> DefaultMovementProfile;

	/**
	 * Stances that own a separate relaxed (sheathed) animation set and therefore keep their MovementProfiles entry while
	 * the weapon is sheathed. Every other stance uses DefaultMovementProfile while sheathed (its sheathed locomotion is the
	 * Neutral set), so the speeds match the clips.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BH|Movement", meta = (Categories = "Stance.Weapon"))
	TSet<FGameplayTag> StancesWithRelaxedSet;

	/** MovementProfiles entry for the current stance (when drawn, or when the stance has a relaxed set), else DefaultMovementProfile (may be null). */
	UFUNCTION(BlueprintPure, Category = "BH|Movement")
	UBH_StanceMovementProfile* GetActiveMovementProfile() const;

	/** Mirror CurrentStance as a loose gameplay tag on the owner's ASC (every machine). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BH|Stance")
	bool bMirrorStanceAsLooseTag = true;

	/** Fires on every machine after the stance is applied (OldStance may be empty on the first apply). */
	UPROPERTY(BlueprintAssignable, Category = "BH|Stance")
	FBH_OnStanceChanged OnStanceChanged;

	UFUNCTION(BlueprintPure, Category = "BH|Stance")
	FGameplayTag GetCurrentStance() const { return CurrentStance; }

	UFUNCTION(BlueprintPure, Category = "BH|Stance")
	FName GetCurrentStanceLegacyName() const;

	UFUNCTION(BlueprintPure, Category = "BH|Stance")
	bool IsStanceAllowed(FGameplayTag Stance) const;

	UFUNCTION(BlueprintPure, Category = "BH|Stance")
	static UBH_StanceComponent* FindStanceComponent(const AActor* Actor);

	/** Current weapon stance of Actor, or Stance.Weapon.Unarmed when it has no stance component. Game thread only. */
	UFUNCTION(BlueprintPure, Category = "BH|Stance")
	static FGameplayTag GetStanceTagOf(const AActor* Actor);

	/** Any machine. Authority sets directly, the owning client asks the server, a simulated proxy returns false. */
	UFUNCTION(BlueprintCallable, Category = "BH|Stance")
	bool RequestStance(FGameplayTag NewStance);

	/** Authority only. Validates, assigns and applies locally. Returns false if rejected. */
	UFUNCTION(BlueprintCallable, Category = "BH|Stance")
	bool SetStance(FGameplayTag NewStance);

	/** Legacy FName/FString call sites ("SwordAndShield", "Greatsword", ...). */
	UFUNCTION(BlueprintCallable, Category = "BH|Stance")
	bool RequestStanceByLegacyName(FName LegacyName);

	// ---- Weapon drawn / sheathed ------------------------------------------------------------------------------

	/** Seconds after the last combat activity before the weapon is sheathed on its own. 0 = never. Not while locked on or mid-combat-ability. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BH|Stance|Weapon State", meta = (ClampMin = 0))
	float AutoSheathDelay = 8.f;

	/** Draw / sheath montages per stance (UpperBody slot). Missing stance or null entry: the state flips without a montage. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BH|Stance|Weapon State", meta = (Categories = "Stance.Weapon"))
	TMap<FGameplayTag, FBH_WeaponStateMontages> WeaponStateMontages;

	/** Fires on every machine when the drawn state changes. */
	UPROPERTY(BlueprintAssignable, Category = "BH|Stance|Weapon State")
	FBH_OnWeaponDrawnChanged OnWeaponDrawnChanged;

	UFUNCTION(BlueprintPure, Category = "BH|Stance|Weapon State")
	bool IsWeaponDrawn() const { return bWeaponDrawn; }

	/** Authority only. Unarmed is always sheathed. Restarts / clears the auto-sheath timer. Returns false if rejected. */
	UFUNCTION(BlueprintCallable, Category = "BH|Stance|Weapon State")
	bool SetWeaponDrawn(bool bDrawn);

	/** Any machine. Authority sets directly, the owning client asks the server, a simulated proxy returns false. */
	UFUNCTION(BlueprintCallable, Category = "BH|Stance|Weapon State")
	bool RequestWeaponDrawn(bool bDrawn);

	UFUNCTION(BlueprintCallable, Category = "BH|Stance|Weapon State")
	void ToggleWeaponDrawn();

	/** Authority only: a combat ability / hit / lock-on happened. Draws the weapon and restarts the auto-sheath timer. */
	UFUNCTION(BlueprintCallable, Category = "BH|Stance|Weapon State")
	void NotifyCombatActivity();

protected:
	UPROPERTY(ReplicatedUsing = OnRep_CurrentStance, VisibleInstanceOnly, BlueprintReadOnly, Category = "BH|Stance")
	FGameplayTag CurrentStance;

	UFUNCTION()
	void OnRep_CurrentStance(FGameplayTag OldStance);

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSetStance(FGameplayTag NewStance);

	/** Server authoritative; mirrored as State.Weapon.Drawn / .Sheathed loose tags. */
	UPROPERTY(ReplicatedUsing = OnRep_WeaponDrawn, VisibleInstanceOnly, BlueprintReadOnly, Category = "BH|Stance|Weapon State")
	bool bWeaponDrawn = false;

	UFUNCTION()
	void OnRep_WeaponDrawn();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSetWeaponDrawn(bool bDrawn);

private:
	void ApplyStanceLocal(FGameplayTag Old, FGameplayTag New);
	void MirrorLooseTag(FGameplayTag Old, FGameplayTag New) const;
	void EquipWeaponsFor(FGameplayTag New) const;
	UAbilitySystemComponent* ResolveASC() const;

	void ApplyWeaponDrawnLocal(bool bDrawn, bool bPlayMontage);
	void RestartAutoSheathTimer();
	void AutoSheathTick();

	FTimerHandle AutoSheathTimer;
};
