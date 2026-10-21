// Blackwood Hollow - overhead enemy vitals widget (implementation)

#include "UI/BH_OverheadVitalsWidget.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Materials/MaterialInstanceDynamic.h"

UBH_OverheadVitalsWidget::UBH_OverheadVitalsWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Scaled-down posture style for a small bar (the shared defaults are tuned for the 420 px target bar).
	PostureStyle.BracketThicknessPx = 1.f;
	PostureStyle.BracketArmPx = 8.f;
	PostureStyle.BracketVerticalArmPx = 3.f;
	PostureStyle.DangerScaleX = 1.05f;
	PostureStyle.BreakPunchScaleX = 1.12f;

	// Enemies have no stance emblem; the root-widget stance poll has nothing to do here.
	StancePollInterval = 3600.f;
}

void UBH_OverheadVitalsWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (Img_HealthBar && !HealthBarMID)
	{
		HealthBarMID = Img_HealthBar->GetDynamicMaterial();
	}
	if (HealthBarMID)
	{
		HealthBarMID->SetVectorParameterValue(TEXT("FillColor"), HealthFillColor);
	}

	UWidget* ScaleRoot = Row_Posture ? Row_Posture.Get() : Cast<UWidget>(Img_PostureBar.Get());
	// No Presenter.Reset(): InitializeHUD may already have pushed posture values before the widget was constructed.
	Presenter.Bind(Img_PostureBar, Img_Brackets, nullptr, ScaleRoot);

	SetRenderOpacity(CurrentOpacity);
	ApplyHealthPercent();
	PresentedSignature = INT32_MIN; // re-applied by the next PresentEnemy
}

void UBH_OverheadVitalsWidget::PresentEnemy(const AActor* Enemy, const AActor* Viewer)
{
	const FBH_EnemyReadability Info = UBH_EnemyReadabilityLibrary::Describe(Enemy, Viewer);
	if (!Info.bValid)
	{
		return;
	}
	const int32 Signature = static_cast<int32>(Info.Rank) | (static_cast<int32>(Info.Archetype) << 4) | (static_cast<int32>(Info.LevelTier) << 8)
		| (Info.Level << 12) | (static_cast<int32>(GetTypeHash(Info.Name.ToString()) & 0x7FF) << 20);
	if (Signature == PresentedSignature)
	{
		return;
	}
	PresentedSignature = Signature;

	const bool bName = UBH_EnemyReadabilityLibrary::RankShowsName(Info.Rank);
	const bool bTitle = UBH_EnemyReadabilityLibrary::RankShowsTitle(Info.Rank) && !Info.Title.IsEmpty();
	if (Txt_Name)
	{
		Txt_Name->SetText(Info.Name);
		Txt_Name->SetVisibility(bName ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (Txt_Title)
	{
		Txt_Title->SetText(Info.Title);
		Txt_Title->SetVisibility(bTitle ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (Plate_Title)
	{
		Plate_Title->SetVisibility(bTitle ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
	K2_OnEnemyPresented(Info.Rank, Info.Archetype, Info.Name, Info.Title, Info.Level, Info.LevelTier);
}

void UBH_OverheadVitalsWidget::SetOverheadOpacity(float Opacity)
{
	Opacity = FMath::Clamp(Opacity, 0.f, 1.f);
	if (!FMath::IsNearlyEqual(Opacity, CurrentOpacity, 1.0e-5f))
	{
		CurrentOpacity = Opacity;
		SetRenderOpacity(CurrentOpacity);
	}
}

void UBH_OverheadVitalsWidget::ApplyHealthPercent()
{
	if (Img_HealthBar && !HealthBarMID)
	{
		HealthBarMID = Img_HealthBar->GetDynamicMaterial();
	}
	if (HealthBarMID)
	{
		HealthBarMID->SetScalarParameterValue(PercentParameterName, DisplayedHealthPercent);
	}
}

void UBH_OverheadVitalsWidget::HandleHealthChanged(float Health, float MaxHealth)
{
	TargetHealthPercent = MaxHealth > 0.f ? FMath::Clamp(Health / MaxHealth, 0.f, 1.f) : 0.f;
	if (HealthEaseSpeed <= 0.f || !bHealthSeeded)
	{
		DisplayedHealthPercent = TargetHealthPercent;
		bHealthSeeded = true;
	}
	ApplyHealthPercent();
}

void UBH_OverheadVitalsWidget::HandlePostureChanged(float Posture, float MaxPosture)
{
	// Same convention as the other posture bars: the fill grows as posture is lost.
	Presenter.SetDamageFraction(MaxPosture > 0.f ? 1.f - Posture / MaxPosture : 0.f);
}

void UBH_OverheadVitalsWidget::HandlePostureBrokenChanged(bool bBroken)
{
	Presenter.SetBroken(bBroken);
}

void UBH_OverheadVitalsWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!FMath::IsNearlyEqual(DisplayedHealthPercent, TargetHealthPercent, 0.0005f))
	{
		DisplayedHealthPercent = HealthEaseSpeed > 0.f
			? FMath::FInterpTo(DisplayedHealthPercent, TargetHealthPercent, InDeltaTime, HealthEaseSpeed)
			: TargetHealthPercent;
		ApplyHealthPercent();
	}

	Presenter.Tick(InDeltaTime, PostureStyle);
}
