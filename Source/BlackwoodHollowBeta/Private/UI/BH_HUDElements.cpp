// Blackwood Hollow - Production HUD elements (implementation)

#include "UI/BH_HUDElements.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystemComponent.h"

// ============================================================================
// UBH_VitalsClusterWidget
// ============================================================================

void UBH_VitalsClusterWidget::NativeConstruct()
{
	Super::NativeConstruct();
	EnsureBarMaterials();
	ApplyHealthPercent();
	ApplyStaminaPercent();
}

void UBH_VitalsClusterWidget::EnsureBarMaterials()
{
	if (Img_HealthBar && !HealthBarMID)
	{
		HealthBarMID = Img_HealthBar->GetDynamicMaterial();
	}
	if (Img_StaminaBar && !StaminaBarMID)
	{
		StaminaBarMID = Img_StaminaBar->GetDynamicMaterial();
	}
}

void UBH_VitalsClusterWidget::ApplyHealthPercent()
{
	EnsureBarMaterials();
	if (HealthBarMID)
	{
		HealthBarMID->SetScalarParameterValue(PercentParameterName, DisplayedHealthPercent);
	}
	if (Bar_Health)
	{
		Bar_Health->SetPercent(DisplayedHealthPercent);
	}
}

void UBH_VitalsClusterWidget::ApplyStaminaPercent()
{
	EnsureBarMaterials();
	if (StaminaBarMID)
	{
		StaminaBarMID->SetScalarParameterValue(PercentParameterName, DisplayedStaminaPercent);
	}
	if (Bar_Stamina)
	{
		Bar_Stamina->SetPercent(DisplayedStaminaPercent);
	}
}

void UBH_VitalsClusterWidget::HandleHealthChanged(float Health, float MaxHealth)
{
	TargetHealthPercent = MaxHealth > 0.f ? FMath::Clamp(Health / MaxHealth, 0.f, 1.f) : 0.f;
	if (HealthEaseSpeed <= 0.f || !bHealthSeeded)
	{
		DisplayedHealthPercent = TargetHealthPercent;
		bHealthSeeded = true;
	}
	ApplyHealthPercent();
	if (Txt_Health)
	{
		Txt_Health->SetVisibility(bShowHealthText ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		Txt_Health->SetText(FText::FromString(FString::Printf(TEXT("%d / %d"), FMath::RoundToInt(Health), FMath::RoundToInt(MaxHealth))));
	}
}

void UBH_VitalsClusterWidget::HandleStaminaChanged(float Stamina, float MaxStamina)
{
	TargetStaminaPercent = MaxStamina > 0.f ? FMath::Clamp(Stamina / MaxStamina, 0.f, 1.f) : 0.f;
	if (StaminaEaseSpeed <= 0.f || !bStaminaSeeded)
	{
		DisplayedStaminaPercent = TargetStaminaPercent;
		bStaminaSeeded = true;
	}
	ApplyStaminaPercent();
	if (Txt_Stamina)
	{
		Txt_Stamina->SetVisibility(bShowStaminaText ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		Txt_Stamina->SetText(FText::FromString(FString::Printf(TEXT("%d / %d"), FMath::RoundToInt(Stamina), FMath::RoundToInt(MaxStamina))));
	}
}

void UBH_VitalsClusterWidget::HandleStanceChanged(const FString& StanceName)
{
	if (!Img_StanceEmblem)
	{
		return;
	}

	UTexture2D* Icon = nullptr;
	for (const TPair<FString, TObjectPtr<UTexture2D>>& Entry : StanceIcons)
	{
		if (Entry.Key.Equals(StanceName, ESearchCase::IgnoreCase))
		{
			Icon = Entry.Value;
			break;
		}
	}

	if (Icon)
	{
		Img_StanceEmblem->SetBrushFromTexture(Icon, /*bMatchSize*/ false);
		Img_StanceEmblem->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	else
	{
		Img_StanceEmblem->SetVisibility(ESlateVisibility::Hidden);
	}
}

void UBH_VitalsClusterWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!FMath::IsNearlyEqual(DisplayedHealthPercent, TargetHealthPercent, 0.0005f))
	{
		DisplayedHealthPercent = HealthEaseSpeed > 0.f
			? FMath::FInterpTo(DisplayedHealthPercent, TargetHealthPercent, InDeltaTime, HealthEaseSpeed)
			: TargetHealthPercent;
		ApplyHealthPercent();
	}

	if (!FMath::IsNearlyEqual(DisplayedStaminaPercent, TargetStaminaPercent, 0.0005f))
	{
		DisplayedStaminaPercent = StaminaEaseSpeed > 0.f
			? FMath::FInterpTo(DisplayedStaminaPercent, TargetStaminaPercent, InDeltaTime, StaminaEaseSpeed)
			: TargetStaminaPercent;
		ApplyStaminaPercent();
	}
}

// ============================================================================
// UBH_ContextualPostureWidget
// ============================================================================

void UBH_ContextualPostureWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Starts hidden (full posture); the first posture loss fades it in. TimeAtFull is pre-charged so the
	// fade logic's target is already 0 on the very first tick (otherwise it would fade IN for FadeOutDelay seconds).
	CurrentOpacity = 0.f;
	TimeAtFull = FadeOutDelay;
	SetRenderOpacity(0.f);
	if (Bar_Posture)
	{
		Bar_Posture->SetFillColorAndOpacity(NormalFillColor);
		Bar_Posture->SetPercent(DamageFraction);
	}
}

void UBH_ContextualPostureWidget::HandlePostureChanged(float Posture, float MaxPosture)
{
	DamageFraction = MaxPosture > 0.f ? FMath::Clamp(1.f - Posture / MaxPosture, 0.f, 1.f) : 0.f;
	const bool bNowFull = DamageFraction <= KINDA_SMALL_NUMBER;
	if (!bNowFull)
	{
		TimeAtFull = 0.f;
	}
	bAtFull = bNowFull;

	if (Bar_Posture && !IsPostureBroken())
	{
		Bar_Posture->SetPercent(DamageFraction);
	}
}

void UBH_ContextualPostureWidget::HandlePostureBrokenChanged(bool bBroken)
{
	FlashTime = 0.f;
	PunchAlpha = bBroken ? 1.f : 0.f;
	if (Bar_Posture)
	{
		Bar_Posture->SetPercent(bBroken ? 1.f : DamageFraction);
		Bar_Posture->SetFillColorAndOpacity(bBroken ? FlashFillColor : NormalFillColor);
		Bar_Posture->SetRenderScale(FVector2D(1.f, 1.f));
	}
}

void UBH_ContextualPostureWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	const bool bBroken = IsPostureBroken();

	// -- Visibility: show while posture is missing, broken, or the owner is engaged in combat;
	//    fade out once all of that has been over for FadeOutDelay.
	bool bEngaged = false;
	if (bShowWhileEngaged)
	{
		if (const UAbilitySystemComponent* ASC = GetHUDAbilitySystem())
		{
			bEngaged = ASC->HasMatchingGameplayTag(TAG_State_Combat_Blocking)
				|| ASC->HasMatchingGameplayTag(TAG_State_Combat_Attacking)
				|| ASC->HasMatchingGameplayTag(TAG_State_Combat_Parrying)
				|| ASC->HasMatchingGameplayTag(TAG_State_Combat_Staggered);
		}
	}

	float TargetOpacity = 1.f;
	if (bAtFull && !bBroken && !bEngaged)
	{
		TimeAtFull += InDeltaTime;
		TargetOpacity = TimeAtFull >= FadeOutDelay ? 0.f : 1.f;
	}
	else
	{
		TimeAtFull = 0.f;
	}
	const float Speed = TargetOpacity > CurrentOpacity ? FadeInSpeed : FadeOutSpeed;
	const float NewOpacity = FMath::FInterpConstantTo(CurrentOpacity, TargetOpacity, InDeltaTime, Speed);
	if (!FMath::IsNearlyEqual(NewOpacity, CurrentOpacity))
	{
		CurrentOpacity = NewOpacity;
		SetRenderOpacity(CurrentOpacity);
	}

	// -- Posture-break flash sequence: colour pulse + a scale punch that settles.
	if (bBroken && Bar_Posture)
	{
		FlashTime += InDeltaTime;
		const float Pulse = 0.5f + 0.5f * FMath::Cos(FlashTime * FlashRate * 2.f * PI);
		Bar_Posture->SetFillColorAndOpacity(FMath::Lerp(BrokenFillColor, FlashFillColor, Pulse));

		if (PunchAlpha > 0.f)
		{
			PunchAlpha = FMath::Max(0.f, PunchAlpha - InDeltaTime * 4.f);
			const float Scale = FMath::Lerp(1.f, BreakPunchScale, PunchAlpha);
			Bar_Posture->SetRenderScale(FVector2D(Scale, Scale));
		}
	}
}
