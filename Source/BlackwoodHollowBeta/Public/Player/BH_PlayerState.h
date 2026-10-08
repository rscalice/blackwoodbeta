// Blackwood Hollow - player state holding the Narrative inventory and the Heart-Fragment slot model
// Target: Unreal Engine 5.8 (C++)
//
// Phase 8B: the PlayerState also owns the replicated Heart-Fragment slot model (3 slots, each restricted to a
// Fragment.Category.* tag). The pawn's UBPC_HeartFragment mirrors it (server) and fires abilities from it; the HUD
// (UBH_FragmentBarWidget) reads it and listens to OnFragmentSlotsChanged.
//
// Narrative Inventory expects a player's UNarrativeInventoryComponent on the PlayerState (it survives pawn
// respawns and replicates to the owning client). GM_BlackwoodHollow uses the Blueprint child
// /Game/Game/PS_BlackwoodHollow.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "GameplayTagContainer.h"
#include "BH_PlayerState.generated.h"

class UNarrativeInventoryComponent;
class UAH_GA_FragmentBase;
class UBH_ProgressionComponent;

/** Broadcast (server and owning client) when a fragment slot changes. SlotIndex -1 = several / unknown slots changed. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBH_OnFragmentSlotsChanged, int32, SlotIndex);

UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API ABH_PlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	ABH_PlayerState();

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Inventory")
	UNarrativeInventoryComponent* GetInventory() const { return Inventory; }

	/** Phase 9: replicated XP / level (survives pawn respawns; applies the level to each new pawn). */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Progression")
	UBH_ProgressionComponent* GetProgression() const { return Progression; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// -- Heart-Fragment slots (Phase 8B) -------------------------------------------------------

	/** Number of fragment slots (keys 1-3). */
	static constexpr int32 NumFragmentSlots = 3;

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Fragments")
	int32 GetNumFragmentSlots() const { return NumFragmentSlots; }

	/** The category restriction of Slot (Fragment.Category.*), or an empty tag for an invalid slot. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Fragments")
	FGameplayTag GetSlotCategory(int32 Slot) const;

	/** The fragment class equipped in Slot (0-2), or null when the slot is empty / out of range. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Fragments")
	TSubclassOf<UAH_GA_FragmentBase> GetFragmentInSlot(int32 Slot) const;

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Fragments")
	bool IsFragmentSlotEmpty(int32 Slot) const;

	/** The category tag a fragment class carries (its CDO's FragmentCategory); empty for null / uncategorised. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Fragments")
	static FGameplayTag GetFragmentCategory(TSubclassOf<UAH_GA_FragmentBase> FragmentClass);

	/** True if Slot exists, FragmentClass is valid, its category matches the slot's restriction, and it is not already equipped in another slot. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Fragments")
	bool CanEquipInSlot(int32 Slot, TSubclassOf<UAH_GA_FragmentBase> FragmentClass) const;

	/** Equips FragmentClass in Slot (replaces what was there). Server applies directly; a client sends ServerEquipFragment. Returns false if CanEquipInSlot fails locally. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Fragments")
	bool EquipFragment(int32 Slot, TSubclassOf<UAH_GA_FragmentBase> FragmentClass);

	/** Empties Slot. Server applies directly; a client sends ServerEquipFragment with a null class. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Fragments")
	bool UnequipFragment(int32 Slot);

	/** Fires the fragment in Slot (0-2) through the pawn's UBPC_HeartFragment. Empty slots do nothing. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Fragments")
	bool ActivateFragmentSlot(int32 Slot);

	/** UI refresh hook: fires whenever a slot is equipped / unequipped (server) or its replicated value changes (clients). */
	UPROPERTY(BlueprintAssignable, Category = "BlackwoodHollow|Fragments")
	FBH_OnFragmentSlotsChanged OnFragmentSlotsChanged;

	// Debug RPCs: declared in every build (UHT forbids UFUNCTION inside #if); bodies are compiled out of Shipping.
	// -- Debug RPCs (non-shipping) -------------------------------------------------------------
	// The bh.* console commands (BH_RPGDebugCommands.cpp) call these from a client window; they act on the SERVER world exactly like
	// the commands do on the host (every player / every wave spawner). Ranges are validated here and pre-checked by the commands.

	static constexpr int32 MaxDebugXP = 1000000;
	static constexpr int32 MaxDebugLevel = 1000;
	static constexpr int32 MaxDebugWave = 1000;

	/** Server: gives Amount XP to every player (bh.XP.Grant from a client). */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerDebugGrantXP(int32 Amount);

	/** Server: sets every player's level (bh.Level.Set from a client). */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerDebugSetLevel(int32 NewLevel);

	/** Server: abandons the current wave and starts WaveNumber (1 = first) on every wave spawner (bh.Arena.SkipToWave from a client). */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerDebugSkipToWave(int32 WaveNumber);

	/** Server: spawns the boss from every wave spawner (bh.Arena.SpawnBoss from a client). */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerDebugSpawnBoss();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BlackwoodHollow|Inventory")
	TObjectPtr<UNarrativeInventoryComponent> Inventory;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BlackwoodHollow|Progression")
	TObjectPtr<UBH_ProgressionComponent> Progression;

	/**
	 * Category restriction per slot (index 0 = key 1). Default {Offensive, Vitality, BlightResist}: Overload Burst (Offensive)
	 * sits in slot 1. Static configuration (not replicated; the PlayerState class defaults are identical on every machine).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BlackwoodHollow|Fragments", meta = (Categories = "Fragment.Category"))
	TArray<FGameplayTag> FragmentSlotCategories;

	/** Equipped fragment per slot (always NumFragmentSlots long; null = empty). Default {OverloadBurst, empty, empty}. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, ReplicatedUsing = OnRep_FragmentSlots, Category = "BlackwoodHollow|Fragments")
	TArray<TSubclassOf<UAH_GA_FragmentBase>> FragmentSlots;

	UFUNCTION()
	void OnRep_FragmentSlots();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerEquipFragment(int32 Slot, TSubclassOf<UAH_GA_FragmentBase> FragmentClass);

private:
	/** Server: validates (except for null = unequip), writes the slot, notifies the UI and pushes the change to the pawn. */
	bool ApplyFragmentSlot(int32 Slot, TSubclassOf<UAH_GA_FragmentBase> FragmentClass);
};
