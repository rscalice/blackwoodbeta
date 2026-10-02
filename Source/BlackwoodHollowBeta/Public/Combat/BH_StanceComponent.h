// Blackwood Hollow - replicated weapon stance (tag keyed) for ABH_CharacterBase descendants
//
// Source of truth is CurrentStance (Stance.Weapon.*). It is mirrored as a loose tag on the owner's ASC on every
// machine, attaches the weapon meshes from WeaponLoadouts and notifies the HUD. Never put this on an actor that also
// has UBH_StanceWatcherComponent (GASPALS path): the watcher would re-equip.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "BH_StanceComponent.generated.h"

class UAbilitySystemComponent;
class UBH_WeaponLoadoutDataAsset;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FBH_OnStanceChanged, FGameplayTag, OldStance, FGameplayTag, NewStance);

UCLASS(ClassGroup = (BlackwoodHollow), meta = (BlueprintSpawnableComponent))
class BLACKWOODHOLLOWBETA_API UBH_StanceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBH_StanceComponent();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Stance applied on the server at BeginPlay when nothing set one earlier. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BH|Stance", meta = (Categories = "Stance.Weapon"))
	FGameplayTag DefaultStance;

	/** Weapon meshes per stance (legacy-keyed loadout asset). Attached on every machine on stance change. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BH|Stance")
	TObjectPtr<UBH_WeaponLoadoutDataAsset> WeaponLoadouts;

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

protected:
	UPROPERTY(ReplicatedUsing = OnRep_CurrentStance, VisibleInstanceOnly, BlueprintReadOnly, Category = "BH|Stance")
	FGameplayTag CurrentStance;

	UFUNCTION()
	void OnRep_CurrentStance(FGameplayTag OldStance);

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSetStance(FGameplayTag NewStance);

private:
	void ApplyStanceLocal(FGameplayTag Old, FGameplayTag New);
	void MirrorLooseTag(FGameplayTag Old, FGameplayTag New) const;
	void EquipWeaponsFor(FGameplayTag New) const;
	UAbilitySystemComponent* ResolveASC() const;
};
