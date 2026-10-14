// Blackwood Hollow - the one notification toast widget (Phase 13C)
// Target: Unreal Engine 5.8 (C++), UMG
//
// Parent of WBP_NotificationQueue (placed once in WBP_HUD_Main). Shows whatever UBH_NotificationSubsystem says is current: it never
// queues anything itself. All named widgets are optional:
//   Panel_Toast  UWidget     the whole toast; faded and slid by this class (collapsed while idle)
//   Txt_Title    UTextBlock  title line (collapsed when empty)
//   Txt_Body     UTextBlock  body / loot lines (collapsed when empty)
//   Img_Icon     UImage      optional icon, styled by the Blueprint event
// Motion: fade in NotificationFadeIn with a 12 px slide, fade out NotificationFadeOut (13A style guide, section 7).
// Colours, icons and sounds come from the Blueprint events (palette tokens only).

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/BH_NotificationTypes.h"
#include "BH_NotificationQueueWidget.generated.h"

class UImage;
class UTextBlock;
class UBH_NotificationSubsystem;

UCLASS(Abstract, Blueprintable)
class BLACKWOODHOLLOWBETA_API UBH_NotificationQueueWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** A toast starts: set the accent colour from the kind / bWarning, the icon, play the sound. */
	UFUNCTION(BlueprintImplementableEvent, Category = "BlackwoodHollow|Notifications", meta = (DisplayName = "On Notification Shown"))
	void K2_OnNotificationShown(const FBH_Notification& Notification);

	/** The showing loot toast absorbed more pickups (a small pulse). */
	UFUNCTION(BlueprintImplementableEvent, Category = "BlackwoodHollow|Notifications", meta = (DisplayName = "On Notification Updated"))
	void K2_OnNotificationUpdated(const FBH_Notification& Notification);

	UFUNCTION(BlueprintImplementableEvent, Category = "BlackwoodHollow|Notifications", meta = (DisplayName = "On Notification Hidden"))
	void K2_OnNotificationHidden();

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional))
	TObjectPtr<UWidget> Panel_Toast;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Title;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> Txt_Body;

	UPROPERTY(BlueprintReadOnly, Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> Img_Icon;

private:
	UFUNCTION()
	void HandleShown(const FBH_Notification& Notification);

	UFUNCTION()
	void HandleUpdated(const FBH_Notification& Notification);

	UFUNCTION()
	void HandleEnding();

	UFUNCTION()
	void HandleFinished();

	void ApplyTexts(const FBH_Notification& Notification);
	void ApplyVisual(float Alpha);

	TWeakObjectPtr<UBH_NotificationSubsystem> BoundQueue;
	float Alpha = 0.f;
	bool bFadingIn = false;
	bool bFadingOut = false;
};
