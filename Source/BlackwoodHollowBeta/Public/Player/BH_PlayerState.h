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
#include "Consumables/BH_ConsumableTypes.h"
#include "BH_PlayerState.generated.h"

class UNarrativeInventoryComponent;
class UNarrativeItem;
class UAH_GA_FragmentBase;
class UBH_ProgressionComponent;

/** Broadcast (server and owning client) when a fragment slot changes. SlotIndex -1 = several / unknown slots changed. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBH_OnFragmentSlotsChanged, int32, SlotIndex);

/** Broadcast (server and every client) when one of the per-player progression gates changes: weapon set B unlock, learned Overload Burst, respawn attunement. For UI. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FBH_OnProgressionGatesChanged);

/** Broadcast on the OWNING player's machine when a consumable use was refused (nothing consumed). Wire a toast / sound to it. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FBH_OnConsumableUseRefused, TSubclassOf<UNarrativeItem>, ItemClass, EBH_ConsumableRefusal, Reason);

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

	// -- Consumable refusal feedback (Phase 11E) -------------------------------------------------

	/** Fires on the owning player's machine when a consumable use is refused (Sap at full health, none left, ability busy). */
	UPROPERTY(BlueprintAssignable, Category = "BlackwoodHollow|Consumable")
	FBH_OnConsumableUseRefused OnConsumableUseRefused;

	/** Any machine. Shows a dev toast + broadcasts OnConsumableUseRefused here when this is the local player's state, else sends a client RPC to the owner. */
	void NotifyConsumableUseRefused(TSubclassOf<UNarrativeItem> ItemClass, EBH_ConsumableRefusal Reason);

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

	// -- Progression gates (Phase 12F) -----------------------------------------------------------
	// Per-player state that the Island 1 rules (ABH_IslandRules) and the world pickups change. All of it lives here (not on the pawn)
	// so it survives death and respawn. Server writes, everything replicates, OnProgressionGatesChanged fires on every machine.

	/** False while weapon set B is locked for this player (Island 1): the loadout refuses to equip into set B and the radial refuses to select it. Default true. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Progression")
	bool IsWeaponSetBUnlocked() const { return bWeaponSetBUnlocked; }

	/** SERVER. Locks / unlocks weapon set B for this player (bh.Loadout.UnlockSetB, ABH_IslandRules). */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "BlackwoodHollow|Progression")
	void SetWeaponSetBUnlocked(bool bUnlocked);

	/** True once this player has learned Overload Burst at a Warden obelisk (or through bh.Skills.GrantBurst). A partner marker reads it. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Progression")
	bool HasLearnedOverloadBurst() const { return bOverloadBurstLearned; }

	/** SERVER. Marks Overload Burst as learned and equips it into the first fragment slot that accepts it. @return true if the fragment ended up equipped. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "BlackwoodHollow|Progression")
	bool GrantOverloadBurst();

	/** True if this player attuned the respawn point(s) of HubName. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Respawn")
	bool IsHubAttuned(FName HubName) const { return AttunedHubs.Contains(HubName); }

	/** True once this player attuned a regular (non-starter) respawn point: the starter camp (ABH_RespawnPoint::bAttunedByDefault) is retired for them. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Respawn")
	bool AreStarterPointsRetired() const { return bStarterPointsRetired; }

	/** True if this player has attuned at least one respawn point of their own. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Respawn")
	bool HasAttunedAnyHub() const { return AttunedHubs.Num() > 0; }

	/** SERVER. Records that this player touched the respawn point of HubName; bRetireStarterPoints also retires the starter camp for them. @return true if anything changed. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "BlackwoodHollow|Respawn")
	bool AttuneHub(FName HubName, bool bRetireStarterPoints);

	/** Fires on every machine whenever one of the gates above changes. */
	UPROPERTY(BlueprintAssignable, Category = "BlackwoodHollow|Progression")
	FBH_OnProgressionGatesChanged OnProgressionGatesChanged;

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

	/** Server: unlocks weapon set B for this player (bh.Loadout.UnlockSetB). */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerDebugUnlockSetB();

	/** Server: grants this player Overload Burst as if they had touched the Warden obelisk (bh.Skills.GrantBurst). */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerDebugGrantBurst();

	/** Server: sets the world flag Island1.SpanGateOpen on the game state (bh.World.OpenSpanGate). */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerDebugOpenSpanGate();

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

	/** Phase 12F. Default true; ABH_IslandRules sets it false for the players of Island 1. */
	UPROPERTY(ReplicatedUsing = OnRep_ProgressionGates)
	bool bWeaponSetBUnlocked = true;

	/** Phase 12F. Set by the Warden obelisk (or bh.Skills.GrantBurst). */
	UPROPERTY(ReplicatedUsing = OnRep_ProgressionGates)
	bool bOverloadBurstLearned = false;

	/** Phase 12F. Respawn hubs (ABH_RespawnPoint::HubName) this player has touched. */
	UPROPERTY(ReplicatedUsing = OnRep_ProgressionGates)
	TArray<FName> AttunedHubs;

	/** Phase 12F. True once the starter respawn camp no longer counts for this player. */
	UPROPERTY(ReplicatedUsing = OnRep_ProgressionGates)
	bool bStarterPointsRetired = false;

	UFUNCTION()
	void OnRep_ProgressionGates();

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerEquipFragment(int32 Slot, TSubclassOf<UAH_GA_FragmentBase> FragmentClass);

	/** Owner only: the server refused a consumable use. */
	UFUNCTION(Client, Unreliable)
	void ClientConsumableUseRefused(TSubclassOf<UNarrativeItem> ItemClass, EBH_ConsumableRefusal Reason);

private:
	/** Server: validates (except for null = unequip), writes the slot, notifies the UI and pushes the change to the pawn. */
	bool ApplyFragmentSlot(int32 Slot, TSubclassOf<UAH_GA_FragmentBase> FragmentClass);

	/** Local machine: dev toast + broadcast. */
	void PresentConsumableRefusal(TSubclassOf<UNarrativeItem> ItemClass, EBH_ConsumableRefusal Reason);
};
