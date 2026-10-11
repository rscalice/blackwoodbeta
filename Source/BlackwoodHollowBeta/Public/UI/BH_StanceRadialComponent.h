// Blackwood Hollow - stance radial menu (RadialSelector plugin) + stance switching for the player controller
// Target: Unreal Engine 5.8 (C++), RadialSelector plugin 1.x
//
// Lives on PC_BlackwoodHollow (use the Blueprint child /Game/BlackwoodHollow/UI/AC_BH_StanceRadial, which carries the
// widget class, layout and input assets). Local-only, like the plugin component it derives from.
//
//   * Rebuilds the wheel (transient URadialSelectorMenuData, one segment per available stance) whenever the
//     controlled pawn's UBH_LoadoutComponent reports new AvailableStances.
//   * If the current stance is no longer available it switches to AvailableStances[0].
//   * SelectStance(): the single stance-switch path used by the wheel, by Tab-tap (CycleStance) and by the
//     "current stance vanished" fallback: RequestStanceByName + update the PC's MeleeAttackAbilityClass.
//   * Tab / D-pad Up: a TAP cycles (IA_SwitchStance, Tap trigger); a HOLD (IA_StanceRadial, Hold trigger) opens the
//     wheel and releasing confirms the hovered segment. The plugin's own open action is not used: its Hold mode
//     opens on Started (i.e. on press), so we bind HoldOpenAction ourselves and call OpenMenu()/CloseMenu().
//
// Phase 8B: optional 8-slot mode (bEightSlotRadial, default OFF so the existing Blueprint wheel is untouched):
//   slot 0 = weapon loadout set A, slot 1 = set B (UBH_LoadoutComponent::GetStanceForSet), slots 2-7 = consumables
//   (ConsumableSlots, empty-safe item reference; the item system arrives in Phase 11). The plugin draws the wedges from
//   MenuData, so no custom radial widget is required; UIs that want more can read GetRadialSlots().
// Phase 8D: the placeholder stances (OneHandedSword / Bow / Crossbow) can be listed (bIncludePlaceholderStancesInWheel)
//   but selecting one only logs "Stance not yet implemented" and keeps the current stance.
// Phase 11E: a consumable slot whose stack is 0 stays in its place (the other wedges keep their angles) but is UNAVAILABLE:
//   FBH_RadialSlotData::bAvailable = false, its label drops the "x0", its wedge is dimmed, and choosing it does nothing (no request,
//   no RPC). A custom wedge widget should grey the icon from bAvailable (GetRadialSlots / IsConsumableSlotAvailable).

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Components/RadialSelectorComponent.h"
#include "BH_StanceRadialComponent.generated.h"

class UBH_LoadoutComponent;
class UInputAction;
class UEnhancedInputComponent;
class URadialSelectorMenuLayout;
class UGameplayAbility;
struct FInputActionValue;

class UTexture2D;

/** What a radial slot holds (8-slot mode). */
UENUM(BlueprintType)
enum class EBH_RadialSlotKind : uint8
{
	WeaponSet,
	Consumable
};

/** One consumable slot of the 8-slot radial (radial slots 2-7). Everything is optional: an empty reference is a valid empty slot. */
USTRUCT(BlueprintType)
struct BLACKWOODHOLLOWBETA_API FBH_RadialConsumableSlot
{
	GENERATED_BODY()

	/** Phase 11 item (Narrative item class / asset). Null = empty slot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable")
	TSoftObjectPtr<UObject> ItemReference;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable")
	TSoftObjectPtr<UTexture2D> Icon;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable")
	int32 Quantity = 0;

	bool IsEmpty() const { return ItemReference.IsNull(); }
};

/** Read-only description of one of the 8 radial slots, for custom UIs. */
USTRUCT(BlueprintType)
struct BLACKWOODHOLLOWBETA_API FBH_RadialSlotData
{
	GENERATED_BODY()

	/** 0-1 = weapon sets A/B, 2-7 = consumables. */
	UPROPERTY(BlueprintReadOnly, Category = "Radial")
	int32 SlotIndex = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Radial")
	EBH_RadialSlotKind Kind = EBH_RadialSlotKind::WeaponSet;

	/** Segment identifier (Weapon_A, Empty_B, Consumable_1, ...). */
	UPROPERTY(BlueprintReadOnly, Category = "Radial")
	FName Identifier;

	/** Weapon slots: the legacy stance name of the set (NAME_None when the set is empty). */
	UPROPERTY(BlueprintReadOnly, Category = "Radial")
	FName StanceName;

	UPROPERTY(BlueprintReadOnly, Category = "Radial")
	FText DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "Radial")
	TSoftObjectPtr<UTexture2D> Icon;

	UPROPERTY(BlueprintReadOnly, Category = "Radial")
	bool bEmpty = true;

	UPROPERTY(BlueprintReadOnly, Category = "Radial")
	int32 Quantity = 0;

	/**
	 * True when choosing this slot does something: a weapon set that holds a stance, or a consumable slot with at least one item.
	 * False for empty slots and for a consumable at count 0 (shown greyed, no "x0", selecting it is a no-op). Widgets: disable / desaturate on false.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Radial")
	bool bAvailable = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FBH_OnRadialSlotsChanged);
/** Phase 11 hook: a consumable slot was chosen on the wheel. ConsumableIndex is 0-5 (radial slot - 2). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FBH_OnConsumableSlotUsed, int32, ConsumableIndex, const FBH_RadialConsumableSlot&, Slot);

UCLASS(Blueprintable, ClassGroup = (BlackwoodHollow), meta = (BlueprintSpawnableComponent))
class BLACKWOODHOLLOWBETA_API UBH_StanceRadialComponent : public URadialSelectorComponent
{
	GENERATED_BODY()

public:
	UBH_StanceRadialComponent();

	/** Hold-triggered action (Tab / D-pad Up, 0.25 s): Triggered opens the wheel, Completed confirms the hovered segment. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|StanceRadial")
	TObjectPtr<UInputAction> HoldOpenAction;

	/** Layout asset given to the generated menu data (e.g. RS_Layout_Normal). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|StanceRadial")
	TObjectPtr<URadialSelectorMenuLayout> StanceLayout;

	/** Friendly wheel labels per stance (Identifier = stance name is always the lookup key). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|StanceRadial")
	TMap<FName, FText> StanceDisplayNames;

	/** Tag-keyed twin of StanceDisplayNames (Stance.Weapon.*); read first, the legacy map is the fallback. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|StanceRadial", meta = (Categories = "Stance.Weapon"))
	TMap<FGameplayTag, FText> StanceDisplayNamesByTag;

	/** Seconds between checks for a (new) controlled pawn / loadout component to bind to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|StanceRadial", meta = (ClampMin = "0.05"))
	float PawnPollInterval = 0.25f;

	/** Number of consumable slots in 8-slot mode (radial slots 2-7). */
	static constexpr int32 NumConsumableSlots = 6;

	/** Total radial slots in 8-slot mode: 2 weapon sets + consumables. */
	static constexpr int32 NumRadialSlots = 2 + NumConsumableSlots;

	/** true = 8-slot wheel (set A, set B, 6 consumables). false (default) = the classic one-segment-per-stance wheel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|StanceRadial|EightSlot")
	bool bEightSlotRadial = false;

	/** Consumable slots 2-7 (index 0 = radial slot 2). Kept at NumConsumableSlots entries. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|StanceRadial|EightSlot")
	TArray<FBH_RadialConsumableSlot> ConsumableSlots;

	/** Classic wheel only: also list OneHandedSword / Bow / Crossbow (selecting them reports "not yet implemented"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|StanceRadial")
	bool bIncludePlaceholderStancesInWheel = false;

	/**
	 * Phase 11D: when true (default) and ALL consumable slots are empty the first time the wheel refreshes, slot 1 is filled with Heartwood Sap and
	 * slot 2 with Warden's Incense. Slots filled in the Blueprint (or by SetConsumableSlot) are never touched. Counts / labels of every filled
	 * slot are refreshed from the local player's inventory whenever the wheel is closed and just before it opens.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|StanceRadial|EightSlot")
	bool bAutoPopulateConsumables = true;

	/** Fires after the wheel content was rebuilt or a consumable slot changed. */
	UPROPERTY(BlueprintAssignable, Category = "BlackwoodHollow|StanceRadial|EightSlot")
	FBH_OnRadialSlotsChanged OnRadialSlotsChanged;

	/** Fires when a consumable wedge is chosen (before the use request is sent to the server). Not fired for an unavailable (count 0) slot. */
	UPROPERTY(BlueprintAssignable, Category = "BlackwoodHollow|StanceRadial|EightSlot")
	FBH_OnConsumableSlotUsed OnConsumableSlotUsed;

	/** Phase 11D: applies the default Sap / Incense slots (see bAutoPopulateConsumables) and refreshes labels, icons and counts from the local inventory. Rebuilds the wheel when something changed (never while it is open). @return true if the wheel was rebuilt. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|StanceRadial|EightSlot")
	bool RefreshConsumableSlots();

	/** The 8 slots as data (valid in 8-slot mode; also usable in classic mode, where it still describes sets A/B and the consumables). */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|StanceRadial|EightSlot")
	TArray<FBH_RadialSlotData> GetRadialSlots() const;

	/** Phase 11E: true when radial slot RadialSlotIndex (2-7) holds an item the local player currently owns (count >= 1). Reads the live inventory. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|StanceRadial|EightSlot")
	bool IsConsumableSlotAvailable(int32 RadialSlotIndex) const;

	/** ConsumableIndex 0-5. Rebuilds the wheel. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|StanceRadial|EightSlot")
	bool SetConsumableSlot(int32 ConsumableIndex, const FBH_RadialConsumableSlot& NewSlot);

	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|StanceRadial|EightSlot")
	bool ClearConsumableSlot(int32 ConsumableIndex);

	/** Radial slot 2-7 chosen on the wheel (Phase 11D): broadcasts OnConsumableSlotUsed and asks the server to use the slot's item through UBH_ConsumableLibrary::RequestUseConsumable. A slot at count 0 does nothing. @return true when a request was sent. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|StanceRadial|EightSlot")
	bool UseConsumableSlot(int32 RadialSlotIndex);

	/** Builds the wheel from Stances: Identifier = stance name, DisplayName = friendly name, Icon = GetStanceIconForPose. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|StanceRadial")
	void RebuildFromStances(const TArray<FName>& Stances);

	/** The one stance-switch path: asks for the overlay pose and updates the PC's MeleeAttackAbilityClass. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|StanceRadial")
	bool SelectStance(FName Stance);

	/** Tab tap: next stance in the loadout's AvailableStances (falls back to the PC's StanceCycle when empty). */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|StanceRadial")
	void CycleStance();

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|StanceRadial")
	int32 GetSegmentCount() const;

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|StanceRadial")
	bool IsWheelOpen() const { return State == ERadialSelectorState::Open; }

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|StanceRadial")
	int32 GetHoveredSegment() const { return HoveredSegmentIndex; }

	/** Identifiers of the current wheel segments, in order. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|StanceRadial")
	TArray<FName> GetSegmentIdentifiers() const;

	/** Opens the wheel (same as the hold). No-op with no segments. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|StanceRadial")
	void OpenWheel();

	/** Closes the wheel; bConfirm selects the hovered segment (if any). */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|StanceRadial")
	void CloseWheel(bool bConfirm);

	/** Test hook: pretend segment Index is hovered while the wheel is open (stands in for mouse/stick aim). */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|StanceRadial")
	void DebugHoverSegment(int32 Index);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void HandleInputBound(UEnhancedInputComponent* InputComponent);

	UFUNCTION()
	void HandleSegmentSelected(const FRadialSelectorSegment& SelectedSegment);

	UFUNCTION()
	void HandleStancesChanged(const TArray<FName>& Stances);

	void HandleHoldTriggered(const FInputActionValue& Value);
	void HandleHoldCompleted(const FInputActionValue& Value);

	void PollPawn();
	APawn* GetControlledPawn() const;
	void RebuildEightSlot();

	/** Live stack count of consumable slot ConsumableIndex (0-5) in the local player's inventory; 0 for an empty / invalid slot. */
	int32 GetLiveConsumableCount(int32 ConsumableIndex) const;

	/** Stance legacy name per weapon slot (index 0 = set A, 1 = set B) as of the last eight-slot rebuild. */
	TArray<FName> WeaponSlotStances;

	/** The default consumable slots were already considered (applied or skipped because the Blueprint filled them). */
	bool bDefaultConsumablesApplied = false;

	UPROPERTY(Transient)
	TObjectPtr<UBH_LoadoutComponent> BoundLoadout;

	FTimerHandle PollTimer;
};
