// Blackwood Hollow - the Island 1 weapon cache (Phase 12F)
// Target: Unreal Engine 5.8 (C++)
//
// A cache in the wreck of the Heartwood Rift. Each player takes from it ONCE (same per-player pattern as ABH_LootContainer: the server
// remembers who took, TakenKeys replicates so a player who already took is not offered the prompt again). Taking grants WeaponItemClass
// into that player's PlayerState inventory and equips it into EquipSlot of THEIR loadout (set A main by default) through
// UBH_LoadoutComponent::ApplyLoadoutPreset, which also switches the stance to the new weapon. The preset path replaces whatever else the
// player had equipped; on Island 1 the players start unarmed, so that is nothing.
//
// WeaponItemClass is a soft class reference so the Sword variant (Phase 12F #41) can be dropped in later without touching C++. Until then it is
// the Longsword item, which gives the Sword & Shield stance's one-handed fallback.
//
// World state, not wipe state: nothing here resets on a party wipe. Only bh.Loot.ResetContainers (debug) clears the taken list.
//
// Setup in a Blueprint child: set BodyMesh (graybox cube), the prompt names on Interactable and WeaponItemClass. Keep the box of the placeholder
// it replaces (the floor under the cache is tilted 6 degrees).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/BH_InteractableComponent.h"
#include "Items/BH_EquipmentTypes.h"
#include "BH_WeaponCache.generated.h"

class APlayerState;
class UBH_WeaponItem;
class UStaticMeshComponent;

UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API ABH_WeaponCache : public AActor, public IBH_InteractableOwner
{
	GENERATED_BODY()

public:
	ABH_WeaponCache();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** The weapon item granted (a Blueprint child of UBH_WeaponItem). Soft, so the Sword variant can replace it later. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|WeaponCache")
	TSoftClassPtr<UBH_WeaponItem> WeaponItemClass = TSoftClassPtr<UBH_WeaponItem>(FSoftObjectPath(TEXT("/Game/BlackwoodHollow/Items/Weapons/BPI_Weapon_Longsword.BPI_Weapon_Longsword_C")));

	/** The slot of the taker's loadout the weapon is equipped into. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|WeaponCache")
	EBH_EquipSlot EquipSlot = EBH_EquipSlot::Weapon_Main_A;

	/** True if this player already took their weapon. Both machines. */
	UFUNCTION(BlueprintPure, Category = "BH|WeaponCache")
	bool HasPlayerTaken(const APlayerState* PlayerState) const;

	/** Fires on every machine when the taken list changes (a player took, or the debug reset). For a lid / glow / sound. */
	UFUNCTION(BlueprintImplementableEvent, Category = "BH|WeaponCache")
	void BP_OnTakenChanged();

	// -- IBH_InteractableOwner -----------------------------------------------------------------
	virtual bool BH_CanInteract(const APawn* InteractingPawn, FText& OutDenyReason) const override;
	virtual void BH_OnInteractionCompleted(APawn* InteractingPawn) override;
	virtual void BH_DebugReset() override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBH_InteractableComponent> Interactable;

private:
	UFUNCTION()
	void OnRep_TakenKeys();

	/** Players that already took their weapon (UBH_LootLibrary::GetPlayerKey). Replicated so clients can hide the prompt. */
	UPROPERTY(ReplicatedUsing = OnRep_TakenKeys)
	TArray<FString> TakenKeys;
};
