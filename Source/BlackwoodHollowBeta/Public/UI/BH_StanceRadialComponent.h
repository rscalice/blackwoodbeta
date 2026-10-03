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

	UPROPERTY(Transient)
	TObjectPtr<UBH_LoadoutComponent> BoundLoadout;

	FTimerHandle PollTimer;
};
