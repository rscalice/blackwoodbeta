// Blackwood Hollow - Heart-Fragment bar (3 slots) widget base
// Target: Unreal Engine 5.8 (C++), UMG
//
// Abstract C++ base for WBP_FragmentBar (horizontal 3-slot bar, keys 1-3). All data and logic live here; the Blueprint
// child only lays out the three slot frames and reacts to K2_OnSlotsChanged / K2_OnCooldownsUpdated (or simply binds its
// icon / progress widgets to the BlueprintPure getters below).
//
//   * Slot content comes from ABH_PlayerState (replicated slot model) and refreshes through OnFragmentSlotsChanged.
//   * Cooldowns are read cheaply from the owning pawn's ASC: UAH_GA_FragmentBase::GetCooldownRemaining, only when the
//     fragment's cooldown tag is present on the ASC, and only every CooldownPollInterval seconds.
//   * Derives from UBH_HUDWidget so it can sit inside WBP_HUD_Main and be initialised by the parent's InitializeHUD
//     (the ASC bound there is used for the cooldown reads; the owning pawn's ASC is the fallback).

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UI/BH_HUDWidget.h"
#include "BH_FragmentBarWidget.generated.h"

class ABH_PlayerState;
class UAbilitySystemComponent;
class UAH_GA_FragmentBase;
class UTexture2D;

/** Snapshot of one fragment slot for UI binding. */
USTRUCT(BlueprintType)
struct BLACKWOODHOLLOWBETA_API FBH_FragmentSlotView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Fragment")
	int32 SlotIndex = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Fragment")
	bool bEmpty = true;

	UPROPERTY(BlueprintReadOnly, Category = "Fragment")
	TSubclassOf<UAH_GA_FragmentBase> Fragment;

	UPROPERTY(BlueprintReadOnly, Category = "Fragment")
	FText Name;

	UPROPERTY(BlueprintReadOnly, Category = "Fragment")
	TObjectPtr<UTexture2D> Icon;

	/** The slot's restriction (Fragment.Category.*): drives the frame colour / category glyph even when the slot is empty. */
	UPROPERTY(BlueprintReadOnly, Category = "Fragment")
	FGameplayTag Category;

	/** The equipped fragment's own category (empty for an empty slot). */
	UPROPERTY(BlueprintReadOnly, Category = "Fragment")
	FGameplayTag FragmentCategory;

	UPROPERTY(BlueprintReadOnly, Category = "Fragment")
	float CooldownRemaining = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Fragment")
	float CooldownDuration = 0.f;

	/** CooldownRemaining / CooldownDuration: 1 right after use, 0 when ready. */
	UPROPERTY(BlueprintReadOnly, Category = "Fragment")
	float CooldownFraction = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Fragment")
	bool bOnCooldown = false;
};

UCLASS(Abstract, Blueprintable)
class BLACKWOODHOLLOWBETA_API UBH_FragmentBarWidget : public UBH_HUDWidget
{
	GENERATED_BODY()

public:
	/** Number of slots shown (keys 1-3). */
	static constexpr int32 NumSlots = 3;

	/** Seconds between cooldown reads. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BlackwoodHollow|FragmentBar", meta = (ClampMin = "0.016"))
	float CooldownPollInterval = 0.05f;

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|FragmentBar")
	int32 GetNumSlots() const { return NumSlots; }

	/** Current snapshot of SlotIndex (0-2). An invalid slot returns a default (empty) view. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|FragmentBar")
	FBH_FragmentSlotView GetSlotView(int32 SlotIndex) const;

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|FragmentBar")
	bool IsSlotEmpty(int32 SlotIndex) const;

	/** Icon of the fragment in SlotIndex (null when empty / no icon authored). */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|FragmentBar")
	UTexture2D* GetSlotIcon(int32 SlotIndex) const;

	/** The slot's category restriction (Fragment.Category.*). */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|FragmentBar")
	FGameplayTag GetSlotCategory(int32 SlotIndex) const;

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|FragmentBar")
	float GetSlotCooldownRemaining(int32 SlotIndex) const;

	/** 1 = just used, 0 = ready (use as the radial / bar fill of the cooldown overlay). */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|FragmentBar")
	float GetSlotCooldownFraction(int32 SlotIndex) const;

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|FragmentBar")
	bool IsSlotOnCooldown(int32 SlotIndex) const;

	/** Forces a full re-read of slot content and cooldowns and fires both K2 events. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|FragmentBar")
	void RefreshAll();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** Slot content changed (equip / unequip / first bind). Re-read GetSlotView for the slots you display. */
	UFUNCTION(BlueprintImplementableEvent, Category = "BlackwoodHollow|FragmentBar", meta = (DisplayName = "On Slots Changed"))
	void K2_OnSlotsChanged();

	/** Cooldown values were refreshed (fires every poll while any slot is on cooldown, and once when the last one finishes). */
	UFUNCTION(BlueprintImplementableEvent, Category = "BlackwoodHollow|FragmentBar", meta = (DisplayName = "On Cooldowns Updated"))
	void K2_OnCooldownsUpdated();

private:
	UFUNCTION()
	void HandleSlotsChanged(int32 SlotIndex);

	void BindToPlayerState();
	void UnbindFromPlayerState();
	void RebuildSlotViews();
	/** @return true if any cooldown value changed. */
	bool UpdateCooldowns();
	const UAbilitySystemComponent* ResolveCooldownASC() const;

	UPROPERTY(Transient)
	TArray<FBH_FragmentSlotView> SlotViews;

	TWeakObjectPtr<ABH_PlayerState> BoundPlayerState;
	float CooldownPollTimer = 0.f;
	bool bAnyOnCooldownLastPoll = false;
};
