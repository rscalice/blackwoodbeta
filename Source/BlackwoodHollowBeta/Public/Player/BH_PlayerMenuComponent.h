// Blackwood Hollow - Player Menu server actions (Phase 13G-0)
// Target: Unreal Engine 5.8 (C++), Narrative Inventory/Equipment
//
// A replicated default subobject of ABH_PlayerState. It gives the Player Menu a client-callable path for the three inventory actions that
// Narrative and the loadout only allow on the server:
//
//   RequestEquipItem(Item, Slot)   weapons -> UBH_LoadoutComponent::EquipWeaponToSlot (two-hander rule included); armor -> activates the piece
//                                  (Narrative swaps out the piece that was in that slot). Weapon set B is refused while locked.
//   RequestUnequipSlot(Slot)       weapons -> UnequipWeaponSlot; armor -> deactivates the active piece of that slot. The item stays in the bag.
//   RequestDropItem(Item, Count)   removes Count from the stack and spawns a world pickup in front of the pawn. Equipped items and quest items
//                                  (tag Item.Quest, or Narrative CanBeRemoved false) are refused.
//
// On a client each Request sends a Server RPC; on the server (host) it runs at once. Every server path re-validates (item belongs to THIS
// player's inventory, slot rules, drop rules), so a modified client cannot equip into the locked set or drop quest items.
// The outcome comes back as OnActionResult(bSuccess, Reason) on the owning machine: bind it to show the refusal toast.
// Pure rules live in UBH_PlayerMenuLibrary (IsEquipSlotAllowed, CanDropItem) so the UI can grey out what the server would refuse.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Items/BH_EquipmentTypes.h"
#include "BH_PlayerMenuComponent.generated.h"

class ABH_PlayerState;
class UNarrativeItem;

/** The server finished an equip / unequip / drop request from this player. bSuccess false = refused; Reason is the toast text (empty on success). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FBH_OnMenuActionResult, bool, bSuccess, FText, Reason);

UCLASS(ClassGroup = (BlackwoodHollow), meta = (BlueprintSpawnableComponent))
class BLACKWOODHOLLOWBETA_API UBH_PlayerMenuComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBH_PlayerMenuComponent();

	/** The menu component of a PlayerState, a Pawn (via its PlayerState) or a Controller. Null when there is none. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|PlayerMenu")
	static UBH_PlayerMenuComponent* FindPlayerMenuComponent(const AActor* Actor);

	/** Equips Item into Slot. Armor ignores a mismatching Slot and is refused (it only fits its own slot). */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|PlayerMenu")
	void RequestEquipItem(UNarrativeItem* Item, EBH_EquipSlot Slot);

	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|PlayerMenu")
	void RequestUnequipSlot(EBH_EquipSlot Slot);

	/** Drops Count of Item (clamped to the stack) as a world pickup. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|PlayerMenu")
	void RequestDropItem(UNarrativeItem* Item, int32 Count);

	/** Fires on the OWNING player's machine when a request finished (host: at once; client: when the server answers). */
	UPROPERTY(BlueprintAssignable, Category = "BlackwoodHollow|PlayerMenu")
	FBH_OnMenuActionResult OnActionResult;

	/** Biggest stack a single drop request may name (anything above is rejected as a bad request). */
	static constexpr int32 MaxDropCount = 9999;

	/** Distance in front of the pawn where a dropped item appears (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|PlayerMenu", meta = (ClampMin = "40"))
	float DropDistance = 120.f;

protected:
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerEquipItem(UNarrativeItem* Item, EBH_EquipSlot Slot);

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerUnequipSlot(EBH_EquipSlot Slot);

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerDropItem(UNarrativeItem* Item, int32 Count);

	/** Owner only: the answer to a request. */
	UFUNCTION(Client, Reliable)
	void ClientActionResult(bool bSuccess, const FText& Reason);

private:
	ABH_PlayerState* GetPlayerState() const;

	/** Server only. Each returns true on success; OutReason is the refusal text otherwise. */
	bool ExecuteEquip(UNarrativeItem* Item, EBH_EquipSlot Slot, FText& OutReason);
	bool ExecuteUnequip(EBH_EquipSlot Slot, FText& OutReason);
	bool ExecuteDrop(UNarrativeItem* Item, int32 Count, FText& OutReason);

	/** Server only: shows the result locally when this is the host's own player, otherwise sends ClientActionResult. */
	void ReportResult(bool bSuccess, const FText& Reason);
};
