// Blackwood Hollow - HUD widget base (GAS-driven, event based)
// Target: Unreal Engine 5.8 (C++), GAS + UMG
//
// Base class for every production HUD widget. InitializeHUD(ASC) binds
// attribute-change delegates on UAH_AttributeSet (Health, MaxHealth, Posture,
// MaxPosture, Stamina, MaxStamina) and the State.Combat.PostureBroken tag, then pushes the current
// values once so the widget is correct immediately.
//
// Nested UBH_HUDWidgets (e.g. WBP_VitalsCluster inside WBP_HUD_Main) are
// initialised automatically by their parent, so only the root needs
// InitializeHUD. Blueprint children react through the K2_On*Updated events;
// C++ children override the Handle* virtuals.
//
// Stance: UBH_CombatFunctionLibrary::ApplyOverlayPoseByDisplayName calls
// BroadcastStanceChanged. The root widget also polls the replicated GASP
// OverlayPose a few times a second so changes that only arrive by replication
// (remote clients) still reach the HUD.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayEffectTypes.h"
#include "GameplayTagContainer.h"
#include "BH_HUDWidget.generated.h"

class UAbilitySystemComponent;

UCLASS(Abstract, Blueprintable)
class BLACKWOODHOLLOWBETA_API UBH_HUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Bind this widget (and every nested UBH_HUDWidget) to an AbilitySystemComponent. Safe to call again with a new ASC (respawn). */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|HUD")
	void InitializeHUD(UAbilitySystemComponent* ASC);

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|HUD")
	UAbilitySystemComponent* GetHUDAbilitySystem() const;

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|HUD")
	float GetHealth() const;

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|HUD")
	float GetMaxHealth() const;

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|HUD")
	float GetStamina() const;

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|HUD")
	float GetMaxStamina() const;

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|HUD")
	float GetPosture() const;

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|HUD")
	float GetMaxPosture() const;

	/** True while State.Combat.PostureBroken is on the ASC (or Posture has hit 0). */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|HUD")
	bool IsPostureBroken() const { return bPostureBroken; }

	/** Enum_OverlayPose display name last pushed to this widget (e.g. "SwordAndShield"). */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|HUD")
	FString GetCurrentStanceName() const { return CurrentStanceName; }

	/** Pushes a stance change into this widget (no-op if unchanged). */
	void NotifyStanceChanged(const FString& StanceName);

	/** Pushes a stance change to every live HUD widget bound to Character's ASC. */
	static void BroadcastStanceChanged(const AActor* Character, const FString& StanceName);

protected:
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void NativeDestruct() override;

	// -- C++ hooks (called before the matching K2 event) -----------------------
	virtual void HandleHealthChanged(float Health, float MaxHealth) {}
	virtual void HandleStaminaChanged(float Stamina, float MaxStamina) {}
	virtual void HandlePostureChanged(float Posture, float MaxPosture) {}
	virtual void HandlePostureBrokenChanged(bool bBroken) {}
	virtual void HandleStanceChanged(const FString& StanceName) {}

	// -- Blueprint events -------------------------------------------------------
	UFUNCTION(BlueprintImplementableEvent, Category = "BlackwoodHollow|HUD", meta = (DisplayName = "On Health Updated"))
	void K2_OnHealthUpdated(float Health, float MaxHealth, float Percent);

	UFUNCTION(BlueprintImplementableEvent, Category = "BlackwoodHollow|HUD", meta = (DisplayName = "On Stamina Updated"))
	void K2_OnStaminaUpdated(float Stamina, float MaxStamina, float Percent);

	UFUNCTION(BlueprintImplementableEvent, Category = "BlackwoodHollow|HUD", meta = (DisplayName = "On Posture Updated"))
	void K2_OnPostureUpdated(float Posture, float MaxPosture, float Percent);

	/** StanceName is the Enum_OverlayPose display name ("SwordAndShield", "DualSword", ...). */
	UFUNCTION(BlueprintImplementableEvent, Category = "BlackwoodHollow|HUD", meta = (DisplayName = "On Stance Updated"))
	void K2_OnStanceUpdated(const FString& StanceName);

	UFUNCTION(BlueprintImplementableEvent, Category = "BlackwoodHollow|HUD", meta = (DisplayName = "On Posture Broken Changed"))
	void K2_OnPostureBrokenChanged(bool bBroken);

	/** Seconds between OverlayPose replication checks (root widget only). */
	UPROPERTY(EditAnywhere, Category = "BlackwoodHollow|HUD", meta = (ClampMin = "0.05"))
	float StancePollInterval = 0.25f;

private:
	void UnbindFromASC();
	void OnAttributeChanged(const FOnAttributeChangeData& ChangeData);
	void OnPostureBrokenTagChanged(const FGameplayTag Tag, int32 NewCount);
	void PushHealth();
	void PushStamina();
	void PushPosture();
	void RefreshPostureBroken();
	void PollStance();

	TWeakObjectPtr<UAbilitySystemComponent> BoundASC;
	FDelegateHandle HealthHandle;
	FDelegateHandle MaxHealthHandle;
	FDelegateHandle StaminaHandle;
	FDelegateHandle MaxStaminaHandle;
	FDelegateHandle PostureHandle;
	FDelegateHandle MaxPostureHandle;
	FDelegateHandle PostureBrokenTagHandle;

	/** Set on widgets initialised by a parent HUD widget; only the root polls stance. */
	bool bIsNestedHUD = false;
	bool bPostureBroken = false;
	FString CurrentStanceName;
	float StancePollTimer = 0.f;
};
