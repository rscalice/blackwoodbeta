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
class USkeletalMeshComponent;

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

	// ---- Sword variant (Phase 12F-2) ----------------------------------------------------------------------------
	// The one-handed Sword is Sword & Shield with the off hand empty. CurrentStance stays SwordShield (so the animation
	// Blueprint, locomotion chooser, dodge, hit reactions, posture break and draw / sheath are shared); this is only a
	// lookup KEY (Stance.Weapon.Sword) for what differs: loadout meshes, guard / parry montages, block drain, melee ability,
	// no shield bash. Derived from the replicated inventory, so it is identical on every machine.

	/** True while the stance is SwordShield and the active loadout set holds a main-hand weapon but nothing in the off hand. */
	UFUNCTION(BlueprintPure, Category = "BH|Stance")
	bool IsSwordVariant() const;

	/** Stance.Weapon.Sword while IsSwordVariant(), else CurrentStance. Use for per-stance lookups that the Sword overrides. */
	UFUNCTION(BlueprintPure, Category = "BH|Stance")
	FGameplayTag GetStanceKey() const;

	/** GetStanceKey() of Actor, or Stance.Weapon.Unarmed without a stance component. Game thread only. */
	UFUNCTION(BlueprintPure, Category = "BH|Stance")
	static FGameplayTag GetStanceKeyOf(const AActor* Actor);

	/** Re-evaluates the Sword variant; when it changed, respawns the weapon meshes for the new key. Called by the loadout reconcile on every machine. */
	void RefreshWeaponVariant();

	UFUNCTION(BlueprintPure, Category = "BH|Stance")
	bool IsStanceAllowed(FGameplayTag Stance) const;

	UFUNCTION(BlueprintPure, Category = "BH|Stance")
	static UBH_StanceComponent* FindStanceComponent(const AActor* Actor);

	/** Current weapon stance of Actor, or Stance.Weapon.Unarmed when it has no stance component. Game thread only. */
	UFUNCTION(BlueprintPure, Category = "BH|Stance")
	static FGameplayTag GetStanceTagOf(const AActor* Actor);

	/**
	 * Phase 8D: reports that a placeholder stance (Bow / Crossbow) was picked. Logs a Warning and shows
	 * "Stance not yet implemented: <Name>" on screen (local machine, development builds). The current stance is left unchanged.
	 */
	UFUNCTION(BlueprintCallable, Category = "BH|Stance")
	static void NotifyStanceNotImplemented(const AActor* Actor, FGameplayTag Stance);

	/** Friendly stance name ("Sword & Shield", "Bow", ...) for messages / UI. Falls back to the last tag segment. */
	UFUNCTION(BlueprintPure, Category = "BH|Stance")
	static FText GetStanceDisplayText(FGameplayTag Stance);

	/** Any machine. Authority sets directly, the owning client asks the server, a simulated proxy returns false. Placeholder stances are refused (see NotifyStanceNotImplemented). */
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

	/** Called by UBH_AN_WeaponAttach on every machine: weapons to the hand (true) or to their sheathed socket (false). */
	void HandleWeaponAttachNotify(bool bToHand);

	/** True while the equipped weapons ride in the hand (local, cosmetic). */
	UFUNCTION(BlueprintPure, Category = "BH|Stance|Weapon State")
	bool AreWeaponsInHand() const { return bWeaponsInHand; }

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
	void EquipWeaponsFor(FGameplayTag New);

	/** The variant the current weapon meshes were spawned for (RefreshWeaponVariant compares against it). */
	bool bSwordVariantApplied = false;
	void ApplyWeaponAttachment(bool bInHand);
	void OnWeaponTransitionMontageEnded(UAnimMontage* Montage, bool bInterrupted);
	UAbilitySystemComponent* ResolveASC() const;

	void ApplyWeaponDrawnLocal(bool bDrawn, bool bPlayMontage);
	void RestartAutoSheathTimer();
	void AutoSheathTick();

	FTimerHandle AutoSheathTimer;
	bool bWeaponsInHand = true;
};
