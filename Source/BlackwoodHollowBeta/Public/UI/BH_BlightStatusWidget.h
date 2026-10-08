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

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "Engine/TimerHandle.h"
#include "BH_BlightStatusWidget.generated.h"

class UAbilitySystemComponent;
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

	TWeakObjectPtr<UAbilitySystemComponent> BoundASC;
	FDelegateHandle BuildupHandle;
	FDelegateHandle RotTagHandle;
	FTimerHandle BindTimer;
	float BindTimerInterval = 0.f;

	int32 CurrentStacks = 0;
	bool bRotActive = false;
	float LastShownRotTenths = -1.f;
};
