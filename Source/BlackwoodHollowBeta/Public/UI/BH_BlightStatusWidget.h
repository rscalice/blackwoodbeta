// Blackwood Hollow - Blight status widget (Phase 10B)
// Target: Unreal Engine 5.8 (C++), UMG
//
// Parent of WBP_BlightStatus. Widget Blueprints bind by widget NAME (all optional):
//   Txt_BlightStacks  UTextBlock  Blight stack count = floor(BlightBuildup / 10), 0..10; hidden at 0
//   Img_BlightIcon    UImage      the Blight build-up icon; shown while stacks > 0
//   Img_RotIcon       UImage      the Blight Rot icon; shown only while State.Status.BlightRot is active
//   Txt_RotTime       UTextBlock  seconds left on the Rot ("7.3"); shown only while the Rot is active
//
// The widget finds the owning player's pawn ASC by itself (the pawn may not exist yet, or may be replaced on respawn), retrying on a
// short timer, then listens to the BlightBuildup attribute and the State.Status.BlightRot tag count on that ASC. The attribute is
// replicated owner-only, which is exactly who owns this widget. Blueprint events fire on stack change, Rot start and Rot end so the
// widget blueprint can play animations. Place it in WBP_HUD_Main (VitalsCluster) near the vitals bars.
//
// Rot vignette (Phase 11B, local player only): RotIntensity eases 0 -> RotVignetteMaxIntensity over RotFadeInSeconds when the Rot starts and back
// to 0 over RotFadeOutSeconds when it ends. OnBlightRotStarted / OnBlightRotEnded / OnRotIntensityChanged expose it to the widget blueprint, and
// when RotVignetteWidgetClass (WBP_RotVignette, parent UBH_RotVignetteWidget) is set the widget creates that full-screen vignette itself
// (added to the player's screen behind the HUD) and drives it with RotIntensity.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "Engine/TimerHandle.h"
#include "BH_BlightStatusWidget.generated.h"

class UAbilitySystemComponent;
class UBH_RotVignetteWidget;
class UImage;
class UTextBlock;
class UTexture2D;
struct FOnAttributeChangeData;

UCLASS(Abstract, Blueprintable)
class BLACKWOODHOLLOWBETA_API UBH_BlightStatusWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Build-up points per displayed stack (stacks = floor(BlightBuildup / this)). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|Blight", meta = (ClampMin = "1.0"))
	float BuildupPerStack = 10.f;

	/** Seconds between attempts to find the owning pawn's ASC while there is none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|Blight", meta = (ClampMin = "0.05", ForceUnits = "s"))
	float BindRetryInterval = 0.25f;

	/** Seconds between checks that the bound ASC is still the owning pawn's (rebinding when it changed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|Blight", meta = (ClampMin = "0.1", ForceUnits = "s"))
	float RebindCheckInterval = 2.f;

	/** Rot icon used when the active Rot status effect carries none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|Blight")
	TObjectPtr<UTexture2D> FallbackRotIcon;

	// -- Rot vignette (local player only) ---------------------------------------------------------------

	/** Full-screen vignette widget created on demand (WBP_RotVignette). Empty = no vignette from C++; the Blueprint can still use RotIntensity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|Blight|Rot")
	TSubclassOf<UBH_RotVignetteWidget> RotVignetteWidgetClass;

	/** Z-order of the vignette on the player's screen. Below 0 keeps it behind the HUD. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|Blight|Rot")
	int32 RotVignetteZOrder = -1;

	/** Intensity the vignette reaches while the Rot is active (0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|Blight|Rot", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RotVignetteMaxIntensity = 1.f;

	/** Seconds for the vignette to go from 0 to full when the Rot starts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|Blight|Rot", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float RotFadeInSeconds = 0.4f;

	/** Seconds for the vignette to go from full to 0 when the Rot ends. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|Blight|Rot", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float RotFadeOutSeconds = 0.8f;

	/** Current eased Rot intensity, 0..1. */
	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|Blight|Rot")
	float RotIntensity = 0.f;

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Blight|Rot")
	float GetRotIntensity() const { return RotIntensity; }

	/** RotIntensity changed (fires every frame while it fades, not while it holds). */
	UFUNCTION(BlueprintImplementableEvent, Category = "BlackwoodHollow|Blight|Rot")
	void OnRotIntensityChanged(float NewIntensity);

	/** Looks for the owning pawn's ASC right now and (re)binds if it differs from the bound one. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Blight")
	void TryBind();

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Blight")
	int32 GetStackCount() const { return CurrentStacks; }

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Blight")
	bool IsRotActive() const { return bRotActive; }

	/** The displayed stack count changed (also fires once on bind). */
	UFUNCTION(BlueprintImplementableEvent, Category = "BlackwoodHollow|Blight")
	void OnBlightStacksChanged(int32 NewStacks, int32 OldStacks);

	/** Blight Rot started (Duration = seconds the Rot will last, or 0 when unknown). */
	UFUNCTION(BlueprintImplementableEvent, Category = "BlackwoodHollow|Blight")
	void OnBlightRotStarted(float Duration);

	/** Blight Rot ended (expired, removed or the pawn died). */
	UFUNCTION(BlueprintImplementableEvent, Category = "BlackwoodHollow|Blight")
	void OnBlightRotEnded();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|Blight", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_BlightStacks;

	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|Blight", meta = (BindWidgetOptional))
	TObjectPtr<UImage> Img_BlightIcon;

	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|Blight", meta = (BindWidgetOptional))
	TObjectPtr<UImage> Img_RotIcon;

	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|Blight", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_RotTime;

private:
	void BindTo(UAbilitySystemComponent* ASC);
	void Unbind();
	void ScheduleBindTimer(float Interval);

	void HandleBuildupChanged(const FOnAttributeChangeData& Data);
	void HandleRotTagChanged(const FGameplayTag Tag, int32 NewCount);

	void SetStacksFromBuildup(float Buildup, bool bInitial);
	void SetRotActive(bool bActive, bool bInitial);
	void RefreshRotVisuals();
	void UpdateRotIntensity(float DeltaTime);
	UBH_RotVignetteWidget* EnsureRotVignette();

	UPROPERTY(Transient)
	TObjectPtr<UBH_RotVignetteWidget> RotVignette;

	TWeakObjectPtr<UAbilitySystemComponent> BoundASC;
	FDelegateHandle BuildupHandle;
	FDelegateHandle RotTagHandle;
	FTimerHandle BindTimer;
	float BindTimerInterval = 0.f;

	int32 CurrentStacks = 0;
	bool bRotActive = false;
	float LastShownRotTenths = -1.f;
};
