// Blackwood Hollow - Production HUD elements (implementation)

#include "UI/BH_HUDElements.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Characters/BH_EnemyBase.h"
#include "Combat/BH_CombatIdentityComponent.h"
#include "AbilitySystem/BH_CombatFunctionLibrary.h"

namespace BH_HUDElements_Private
{
	const FName ShearDegreesParam(TEXT("ShearDegrees"));
	const FName AspectRatioParam(TEXT("AspectRatio"));
	const FName FillColorParam(TEXT("FillColor"));
	const FName PercentParam(TEXT("Percent"));
	const FName ColorParam(TEXT("Color"));
	const FName PulseParam(TEXT("Pulse"));
	const FName SlantParam(TEXT("Slant"));
	const FName ThicknessParam(TEXT("Thickness"));
	const FName ArmLengthParam(TEXT("ArmLength"));
	const FName ArmLengthVParam(TEXT("ArmLengthV"));

	FVector2D GetImageSize(const UImage* Image)
	{
		return Image ? FVector2D(Image->GetCachedGeometry().GetLocalSize()) : FVector2D::ZeroVector;
	}
}

// ============================================================================
// FBH_TwoToneRatioText
// ============================================================================

void FBH_TwoToneRatioText::Bind(UTextBlock* InLight, UTextBlock* InDark, UWidget* InClip, const FBH_TwoToneTextStyle& InStyle)
{
	Light = InLight;
	Dark = InDark;
	Clip = InClip;
	Style = InStyle;
	LastWidth = -1.f;
	LastFixedSize = FVector2D::ZeroVector;

	if (!InLight)
	{
		return;
	}

	FSlateFontInfo BaseFont = InLight->GetFont();
	const auto Apply = [&BaseFont](UTextBlock* Text, const FLinearColor& Color, const FLinearColor& Outline)
	{
		FSlateFontInfo Font = BaseFont;
		Font.OutlineSettings.OutlineColor = Outline;
		Text->SetFont(Font);
		Text->SetColorAndOpacity(FSlateColor(Color));
	};

	if (HasSplit())
	{
		Apply(InLight, Style.LightColor, Style.LightOutlineColor);
		Apply(InDark, Style.DarkColor, Style.DarkOutlineColor);
	}
	else
	{
		// Missing fill parts: degrade to the original single dark text.
		Apply(InLight, Style.DarkColor, Style.DarkOutlineColor);
	}
}

FText FBH_TwoToneRatioText::FormatRatio(float Current, float Max)
{
	return FText::FromString(FString::Printf(TEXT("%d / %d"), FMath::RoundToInt(Current), FMath::RoundToInt(Max)));
}

void FBH_TwoToneRatioText::SetRatio(float Current, float Max, bool bVisible)
{
	bShown = bVisible;
	LastWidth = -1.f; // force the clip visibility to be re-applied on the next UpdateSplit

	const FText Text = FormatRatio(Current, Max);
	const ESlateVisibility Vis = bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
	if (UTextBlock* L = Light.Get())
	{
		L->SetVisibility(Vis);
		L->SetText(Text);
	}
	if (UTextBlock* D = Dark.Get())
	{
		D->SetText(Text);
	}
	if (UWidget* C = Clip.Get())
	{
		if (!bVisible)
		{
			C->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

float FBH_TwoToneRatioText::ComputeShearDegrees(const UMaterialInstanceDynamic* BarMID, const FVector2D& BarSize, float SlantSign)
{
	if (!BarMID || BarSize.X < 1.0 || BarSize.Y < 1.0)
	{
		return 0.f;
	}

	// M_UI_SlantedBar shifts the fill edge by (v - 0.5) * tan(ShearDegrees) / AspectRatio in U. In pixels that is
	// dx/dy = -tan(ShearDegrees) * (W / H) / AspectRatio (top of the bar is further left for the default -20 deg).
	UMaterialInstanceDynamic* MID = const_cast<UMaterialInstanceDynamic*>(BarMID);
	const float ShearParamDeg = MID->K2_GetScalarParameterValue(BH_HUDElements_Private::ShearDegreesParam);
	float AspectRatio = MID->K2_GetScalarParameterValue(BH_HUDElements_Private::AspectRatioParam);
	if (AspectRatio < 0.01f)
	{
		AspectRatio = 10.f;
	}
	const float SlopePx = -FMath::Tan(FMath::DegreesToRadians(ShearParamDeg)) * static_cast<float>(BarSize.X / BarSize.Y) / AspectRatio;
	return FMath::RadiansToDegrees(FMath::Atan(SlopePx)) * SlantSign;
}

void FBH_TwoToneRatioText::UpdateSplit(const FVector2D& BarSize, float FillPercent, float ShearDegrees)
{
	UTextBlock* D = Dark.Get();
	UWidget* C = Clip.Get();
	if (!D || !C || BarSize.X < 1.0 || BarSize.Y < 1.0)
	{
		return;
	}

	const float Shear = Style.bFollowBarSlant ? ShearDegrees : 0.f;
	const float Width = static_cast<float>(BarSize.X) * FMath::Clamp(FillPercent, 0.f, 1.f);
	const bool bSizeChanged = !BarSize.Equals(LastFixedSize, 0.01);
	if (!bSizeChanged && FMath::IsNearlyEqual(Width, LastWidth, 0.01f) && FMath::IsNearlyEqual(Shear, LastShear, 0.001f))
	{
		return;
	}
	LastWidth = Width;
	LastShear = Shear;

	// Clip: width = bar width x fill, sheared like the bar.
	if (USizeBox* Box = Cast<USizeBox>(C))
	{
		Box->SetWidthOverride(Width);
	}
	C->SetVisibility((bShown && Width >= 0.5f) ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	C->SetRenderShear(FVector2D(Shear, 0.f));

	// Fixed overlay (parent of the dark text): full bar size so both copies lay out identically; counter-sheared.
	if (UWidget* Fixed = D->GetParent())
	{
		if (bSizeChanged)
		{
			if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Fixed->Slot))
			{
				CanvasSlot->SetAutoSize(false);
				CanvasSlot->SetPosition(FVector2D::ZeroVector);
				CanvasSlot->SetSize(BarSize);
			}
			LastFixedSize = BarSize;
		}
		Fixed->SetRenderShear(FVector2D(-Shear, 0.f));
	}
}

// ============================================================================
// FBH_PostureBarPresenter
// ============================================================================

void FBH_PostureBarPresenter::Bind(UImage* InBar, UImage* InBrackets, UProgressBar* InFallbackBar, UWidget* InScaleTarget)
{
	BarImage = InBar;
	BracketImage = InBrackets;
	FallbackBar = InFallbackBar;
	ScaleTarget = InScaleTarget;
	BarMID = InBar ? InBar->GetDynamicMaterial() : nullptr;
	BracketMID = InBrackets ? InBrackets->GetDynamicMaterial() : nullptr;
	LastBarSize = FVector2D::ZeroVector;
	LastBracketSize = FVector2D::ZeroVector;
}

void FBH_PostureBarPresenter::Reset()
{
	DamageFraction = 0.f;
	bBroken = false;
	DangerAlpha = 0.f;
	PulseTime = 0.f;
	FlashTime = 0.f;
	PunchAlpha = 0.f;
	CurrentScaleX = 1.f;
	if (UWidget* Target = ScaleTarget.Get())
	{
		Target->SetRenderScale(FVector2D(1.f, 1.f));
	}
}

void FBH_PostureBarPresenter::SetBroken(bool bInBroken)
{
	bBroken = bInBroken;
	FlashTime = 0.f;
	PunchAlpha = bInBroken ? 1.f : 0.f;
}

void FBH_PostureBarPresenter::SyncGeometryParams(const FBH_PostureBarStyle& Style)
{
	// Keep the materials' slant true to the real pixel aspect, and the bracket frame in step with the bar's slant.
	if (UMaterialInstanceDynamic* Bar = BarMID.Get())
	{
		const FVector2D Size = BH_HUDElements_Private::GetImageSize(BarImage.Get());
		if (Size.X > 1.0 && Size.Y > 1.0 && !Size.Equals(LastBarSize, 0.01))
		{
			LastBarSize = Size;
			Bar->SetScalarParameterValue(BH_HUDElements_Private::AspectRatioParam, static_cast<float>(Size.X / Size.Y));
		}
	}
	if (UMaterialInstanceDynamic* Brackets = BracketMID.Get())
	{
		const FVector2D Size = BH_HUDElements_Private::GetImageSize(BracketImage.Get());
		if (Size.X > 1.0 && Size.Y > 1.0 && !Size.Equals(LastBracketSize, 0.01))
		{
			LastBracketSize = Size;
			Brackets->SetScalarParameterValue(BH_HUDElements_Private::AspectRatioParam, static_cast<float>(Size.X / Size.Y));
			if (Style.BracketThicknessPx > 0.f)
			{
				Brackets->SetScalarParameterValue(BH_HUDElements_Private::ThicknessParam, Style.BracketThicknessPx / static_cast<float>(Size.Y));
			}
			if (Style.BracketArmPx > 0.f)
			{
				Brackets->SetScalarParameterValue(BH_HUDElements_Private::ArmLengthParam, Style.BracketArmPx / static_cast<float>(Size.X));
			}
			if (Style.BracketVerticalArmPx > 0.f)
			{
				Brackets->SetScalarParameterValue(BH_HUDElements_Private::ArmLengthVParam, FMath::Min(0.45f, Style.BracketVerticalArmPx / static_cast<float>(Size.Y)));
			}
			if (const UMaterialInstanceDynamic* Bar = BarMID.Get())
			{
				const float ShearDeg = const_cast<UMaterialInstanceDynamic*>(Bar)->K2_GetScalarParameterValue(BH_HUDElements_Private::ShearDegreesParam);
				Brackets->SetScalarParameterValue(BH_HUDElements_Private::SlantParam, -FMath::Tan(FMath::DegreesToRadians(ShearDeg)));
			}
		}
	}
}

void FBH_PostureBarPresenter::Tick(float DeltaTime, const FBH_PostureBarStyle& Style)
{
	SyncGeometryParams(Style);

	// Danger: within (1 - DangerThreshold) of breaking (the bar fills toward break), or already broken.
	const bool bDanger = bBroken || DamageFraction >= Style.DangerThreshold;
	DangerAlpha = FMath::FInterpConstantTo(DangerAlpha, bDanger ? 1.f : 0.f, DeltaTime, 1.f / FMath::Max(Style.DangerEaseTime, 0.01f));
	const float Eased = FMath::InterpEaseInOut(0.f, 1.f, DangerAlpha, 2.f);

	PulseTime += DeltaTime;
	PunchAlpha = FMath::Max(0.f, PunchAlpha - DeltaTime / FMath::Max(Style.BreakPunchDuration, 0.01f));

	// -- Fill: break flash (existing behaviour) > danger lerp > normal.
	FLinearColor Fill;
	if (bBroken)
	{
		FlashTime += DeltaTime;
		const float Pulse = 0.5f + 0.5f * FMath::Cos(FlashTime * Style.FlashRate * 2.f * PI);
		Fill = FMath::Lerp(Style.BrokenFillColor, Style.FlashFillColor, Pulse);
	}
	else
	{
		Fill = FMath::Lerp(Style.NormalFillColor, Style.DangerColor, Eased);
	}
	const float Percent = bBroken ? 1.f : DamageFraction;

	if (UMaterialInstanceDynamic* Bar = BarMID.Get())
	{
		Bar->SetScalarParameterValue(BH_HUDElements_Private::PercentParam, Percent);
		Bar->SetVectorParameterValue(BH_HUDElements_Private::FillColorParam, Fill);
	}
	if (UProgressBar* Legacy = FallbackBar.Get())
	{
		Legacy->SetPercent(Percent);
		Legacy->SetFillColorAndOpacity(Fill);
	}

	// -- Brackets: crimson + pulsing in danger; a full glow flash on the break punch.
	if (UMaterialInstanceDynamic* Brackets = BracketMID.Get())
	{
		const float Wave = 0.5f + 0.5f * FMath::Sin(PulseTime * Style.DangerPulseHz * 2.f * PI);
		const float Pulse = FMath::Max(Eased * Wave, PunchAlpha);
		Brackets->SetVectorParameterValue(BH_HUDElements_Private::ColorParam, FMath::Lerp(Style.NormalBracketColor, Style.DangerColor, Eased));
		Brackets->SetScalarParameterValue(BH_HUDElements_Private::PulseParam, Pulse);
	}

	// -- Whole bar expands horizontally in danger; the break punch overshoots then settles back to the danger scale.
	const float DangerScale = FMath::Lerp(1.f, Style.DangerScaleX, Eased);
	const float ScaleX = FMath::Lerp(DangerScale, Style.BreakPunchScaleX, PunchAlpha);
	if (!FMath::IsNearlyEqual(ScaleX, CurrentScaleX, 1.0e-6f))
	{
		CurrentScaleX = ScaleX;
		if (UWidget* Target = ScaleTarget.Get())
		{
			Target->SetRenderScale(FVector2D(CurrentScaleX, 1.f));
		}
	}
}

// ============================================================================
// UBH_VitalsClusterWidget
// ============================================================================

void UBH_VitalsClusterWidget::NativeConstruct()
{
	Super::NativeConstruct();
	EnsureBarMaterials();
	HealthText.Bind(Txt_Health, Txt_HealthFill, Clip_HealthFill, RatioTextStyle);
	StaminaText.Bind(Txt_Stamina, Txt_StaminaFill, Clip_StaminaFill, RatioTextStyle);
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
	RefreshHealthSplit();
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
	RefreshStaminaSplit();
}

void UBH_VitalsClusterWidget::RefreshHealthSplit()
{
	if (HealthText.HasSplit() && Img_HealthBar)
	{
		const FVector2D Size = BH_HUDElements_Private::GetImageSize(Img_HealthBar);
		HealthText.UpdateSplit(Size, DisplayedHealthPercent, FBH_TwoToneRatioText::ComputeShearDegrees(HealthBarMID, Size, RatioTextStyle.SlantSign));
	}
}

void UBH_VitalsClusterWidget::RefreshStaminaSplit()
{
	if (StaminaText.HasSplit() && Img_StaminaBar)
	{
		const FVector2D Size = BH_HUDElements_Private::GetImageSize(Img_StaminaBar);
		StaminaText.UpdateSplit(Size, DisplayedStaminaPercent, FBH_TwoToneRatioText::ComputeShearDegrees(StaminaBarMID, Size, RatioTextStyle.SlantSign));
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
	HealthText.SetRatio(Health, MaxHealth, bShowHealthText);
	ApplyHealthPercent();
}

void UBH_VitalsClusterWidget::HandleStaminaChanged(float Stamina, float MaxStamina)
{
	TargetStaminaPercent = MaxStamina > 0.f ? FMath::Clamp(Stamina / MaxStamina, 0.f, 1.f) : 0.f;
	if (StaminaEaseSpeed <= 0.f || !bStaminaSeeded)
	{
		DisplayedStaminaPercent = TargetStaminaPercent;
		bStaminaSeeded = true;
	}
	StaminaText.SetRatio(Stamina, MaxStamina, bShowStaminaText);
	ApplyStaminaPercent();
}

void UBH_VitalsClusterWidget::HandleStanceChanged(const FString& StanceName)
{
	if (!Img_StanceEmblem)
	{
		return;
	}

	// Data-driven icon first (DA_WeaponLoadouts StanceIcon), then the widget's own StanceIcons map as a fallback.
	UTexture2D* Icon = nullptr;
	if (const UAbilitySystemComponent* ASC = GetHUDAbilitySystem())
	{
		Icon = UBH_CombatFunctionLibrary::GetStanceIconForPose(ASC->GetAvatarActor(), FName(*StanceName));
	}
	if (!Icon)
	{
		const FGameplayTag StanceTag = BH_Stance::FromLegacyName(FName(*StanceName));
		if (const TObjectPtr<UTexture2D>* ByTag = StanceTag.IsValid() ? StanceIconsByTag.Find(StanceTag) : nullptr)
		{
			Icon = ByTag->Get();
		}
	}
	if (!Icon)
	{
		for (const TPair<FString, TObjectPtr<UTexture2D>>& Entry : StanceIcons)
		{
			if (Entry.Key.Equals(StanceName, ESearchCase::IgnoreCase))
			{
				Icon = Entry.Value;
				break;
			}
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
	else
	{
		RefreshHealthSplit(); // cheap; picks up the bar's first real layout size
	}

	if (!FMath::IsNearlyEqual(DisplayedStaminaPercent, TargetStaminaPercent, 0.0005f))
	{
		DisplayedStaminaPercent = StaminaEaseSpeed > 0.f
			? FMath::FInterpTo(DisplayedStaminaPercent, TargetStaminaPercent, InDeltaTime, StaminaEaseSpeed)
			: TargetStaminaPercent;
		ApplyStaminaPercent();
	}
	else
	{
		RefreshStaminaSplit();
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

	Presenter.Bind(Img_PostureBar, Img_Brackets, Img_PostureBar ? nullptr : Bar_Posture.Get(), this);
	Presenter.Reset();
	if (Bar_Posture && Img_PostureBar)
	{
		Bar_Posture->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UBH_ContextualPostureWidget::HandlePostureChanged(float Posture, float MaxPosture)
{
	const float DamageFraction = MaxPosture > 0.f ? FMath::Clamp(1.f - Posture / MaxPosture, 0.f, 1.f) : 0.f;
	Presenter.SetDamageFraction(DamageFraction);

	const bool bNowFull = DamageFraction <= KINDA_SMALL_NUMBER;
	if (!bNowFull)
	{
		TimeAtFull = 0.f;
	}
	bAtFull = bNowFull;
}

void UBH_ContextualPostureWidget::HandlePostureBrokenChanged(bool bBroken)
{
	Presenter.SetBroken(bBroken);
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

	// -- Fill, brackets, danger warning and break flash / punch.
	Presenter.Tick(InDeltaTime, PostureStyle);
}

// ============================================================================
// UBH_TargetVitalsWidget
// ============================================================================

UBH_TargetVitalsWidget::UBH_TargetVitalsWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bExcludeFromParentInit = true;
}

void UBH_TargetVitalsWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (Img_HealthBar && !HealthBarMID)
	{
		HealthBarMID = Img_HealthBar->GetDynamicMaterial();
	}
	HealthText.Bind(Txt_Health, Txt_HealthFill, Clip_HealthFill, RatioTextStyle);

	UWidget* ScaleRoot = Row_Posture ? Row_Posture.Get() : (Img_PostureBar ? Cast<UWidget>(Img_PostureBar.Get()) : Cast<UWidget>(Bar_Posture.Get()));
	Presenter.Bind(Img_PostureBar, Img_Brackets, Img_PostureBar ? nullptr : Bar_Posture.Get(), ScaleRoot);
	Presenter.Reset();
	if (Bar_Posture && Img_PostureBar)
	{
		Bar_Posture->SetVisibility(ESlateVisibility::Collapsed);
	}

	bHasTarget = false;
	CurrentOpacity = 0.f;
	SetRenderOpacity(0.f);
	SetVisibility(ESlateVisibility::Collapsed);
}

void UBH_TargetVitalsWidget::ApplyHealthPercent()
{
	if (Img_HealthBar && !HealthBarMID)
	{
		HealthBarMID = Img_HealthBar->GetDynamicMaterial();
	}
	if (HealthBarMID)
	{
		HealthBarMID->SetScalarParameterValue(PercentParameterName, DisplayedHealthPercent);
	}
	RefreshHealthSplit();
}

void UBH_TargetVitalsWidget::RefreshHealthSplit()
{
	if (HealthText.HasSplit() && Img_HealthBar)
	{
		const FVector2D Size = BH_HUDElements_Private::GetImageSize(Img_HealthBar);
		HealthText.UpdateSplit(Size, DisplayedHealthPercent, FBH_TwoToneRatioText::ComputeShearDegrees(HealthBarMID, Size, RatioTextStyle.SlantSign));
	}
}

void UBH_TargetVitalsWidget::HandleHealthChanged(float Health, float MaxHealth)
{
	TargetHealthPercent = MaxHealth > 0.f ? FMath::Clamp(Health / MaxHealth, 0.f, 1.f) : 0.f;
	if (HealthEaseSpeed <= 0.f || !bHealthSeeded)
	{
		DisplayedHealthPercent = TargetHealthPercent;
		bHealthSeeded = true;
	}
	HealthText.SetRatio(Health, MaxHealth, bShowHealthText);
	ApplyHealthPercent();
}

void UBH_TargetVitalsWidget::HandlePostureChanged(float Posture, float MaxPosture)
{
	// Same convention as UBH_ContextualPostureWidget: the fill grows as posture is lost.
	Presenter.SetDamageFraction(MaxPosture > 0.f ? 1.f - Posture / MaxPosture : 0.f);
}

void UBH_TargetVitalsWidget::HandlePostureBrokenChanged(bool bBroken)
{
	Presenter.SetBroken(bBroken);
}

void UBH_TargetVitalsWidget::HandleLockedTargetChanged(AActor* Target)
{
	Presenter.Reset(); // a new lock starts from the idle look; InitializeHUD below pushes the new target's values

	// A boss shows the top boss bar (UBH_BossHealthBarWidget) instead: hard-locking it must not also raise this panel.
	const UBH_CombatIdentityComponent* LockedIdentity = Target ? UBH_CombatIdentityComponent::Find(Target) : nullptr;
	if (!Target || (LockedIdentity && LockedIdentity->bIsBoss))
	{
		bHasTarget = false;
		InitializeHUD(nullptr);
		SetVisibility(ESlateVisibility::Collapsed);
		CurrentOpacity = 0.f;
		SetRenderOpacity(0.f);
		return;
	}

	bHealthSeeded = false; // snap to the new target's health instead of easing from the old one
	InitializeHUD(UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Target));

	if (Txt_Name)
	{
		// Identity component name first, then the legacy ABH_EnemyBase name, then the actor name.
		const UBH_CombatIdentityComponent* Identity = UBH_CombatIdentityComponent::Find(Target);
		const ABH_EnemyBase* Enemy = Cast<ABH_EnemyBase>(Target);
		Txt_Name->SetText(Identity && !Identity->DisplayName.IsEmpty() ? Identity->DisplayName
			: Enemy ? Enemy->DisplayName : FText::FromString(Target->GetName()));
	}

	if (!bHasTarget)
	{
		CurrentOpacity = 0.f;
		SetRenderOpacity(0.f);
	}
	bHasTarget = true;
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UBH_TargetVitalsWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (bHasTarget && CurrentOpacity < 1.f)
	{
		CurrentOpacity = FMath::FInterpConstantTo(CurrentOpacity, 1.f, InDeltaTime, FadeInSpeed);
		SetRenderOpacity(CurrentOpacity);
	}

	if (!FMath::IsNearlyEqual(DisplayedHealthPercent, TargetHealthPercent, 0.0005f))
	{
		DisplayedHealthPercent = HealthEaseSpeed > 0.f
			? FMath::FInterpTo(DisplayedHealthPercent, TargetHealthPercent, InDeltaTime, HealthEaseSpeed)
			: TargetHealthPercent;
		ApplyHealthPercent();
	}
	else
	{
		RefreshHealthSplit();
	}

	if (bHasTarget)
	{
		Presenter.Tick(InDeltaTime, PostureStyle);
	}
}
