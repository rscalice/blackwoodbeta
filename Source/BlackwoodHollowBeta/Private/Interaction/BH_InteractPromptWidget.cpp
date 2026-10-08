// Blackwood Hollow - interaction prompt base widget (implementation)

#include "Interaction/BH_InteractPromptWidget.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Materials/MaterialInstanceDynamic.h"

void UBH_InteractPromptWidget::SetPrompt(const FText& Name, const FText& Action, bool bCanInteract, const FText& DenyReason, bool bHasHold)
{
	if (Txt_Name)
	{
		Txt_Name->SetText(Name);
	}
	if (Txt_Action)
	{
		Txt_Action->SetText(Action);
		Txt_Action->SetVisibility(bCanInteract ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (Txt_Denied)
	{
		Txt_Denied->SetText(DenyReason);
		Txt_Denied->SetVisibility((!bCanInteract && !DenyReason.IsEmpty()) ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	bShowingHold = bHasHold && bCanInteract;
	const ESlateVisibility HoldVisibility = bShowingHold ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
	if (Pb_Hold)
	{
		Pb_Hold->SetVisibility(HoldVisibility);
	}
	if (Img_HoldRing)
	{
		Img_HoldRing->SetVisibility(HoldVisibility);
	}

	SetVisibility(ESlateVisibility::HitTestInvisible);
	BP_OnPromptShown(bCanInteract);
}

void UBH_InteractPromptWidget::SetHoldProgress(float Fraction)
{
	const float Clamped = FMath::Clamp(Fraction, 0.f, 1.f);
	if (Pb_Hold)
	{
		Pb_Hold->SetPercent(Clamped);
	}
	if (Img_HoldRing)
	{
		if (!HoldRingMID)
		{
			HoldRingMID = Img_HoldRing->GetDynamicMaterial();
		}
		if (HoldRingMID)
		{
			HoldRingMID->SetScalarParameterValue(HoldRingProgressParameter, Clamped);
		}
	}
}

void UBH_InteractPromptWidget::HidePrompt()
{
	bShowingHold = false;
	SetVisibility(ESlateVisibility::Collapsed);
	BP_OnPromptHidden();
}
