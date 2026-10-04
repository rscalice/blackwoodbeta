// Blackwood Hollow - boss health bar (implementation)

#include "UI/BH_BossHealthBarWidget.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Combat/BH_CombatIdentityComponent.h"
#include "GameFramework/Actor.h"

namespace
{
	const FName ShearDegreesParam(TEXT("ShearDegrees"));
	const FName AspectRatioParam(TEXT("AspectRatio"));

	FVector2D GetLocalSize(const UImage* Image)
	{
		return Image ? FVector2D(Image->GetCachedGeometry().GetLocalSize()) : FVector2D::ZeroVector;
	}
}

UBH_BossHealthBarWidget::UBH_BossHealthBarWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Bound by the HUD subsystem to the boss's ASC, never by a parent HUD widget (which would bind it to the local pawn).
	bExcludeFromParentInit = true;
}

// ============================================================================
// Setup
// ============================================================================

void UBH_BossHealthBarWidget::EnsureBarMaterials()
{
	// Dynamic instances of the bars' brush materials, with the slant written (idempotent; the brushes get their materials once in NativeConstruct).
	if (Img_HealthBar && !HealthBarMID)
	{
		HealthBarMID = Img_HealthBar->GetDynamicMaterial();
		if (HealthBarMID)
		{
			HealthBarMID->SetScalarParameterValue(ShearDegreesParam, SlantDegrees);
		}
	}
	if (Img_HealthTrail && !TrailBarMID)
	{
		TrailBarMID = Img_HealthTrail->GetDynamicMaterial();
		if (TrailBarMID)
		{
			TrailBarMID->SetScalarParameterValue(ShearDegreesParam, SlantDegrees);
		}
	}
}

void UBH_BossHealthBarWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Materials first: the posture presenter reads the bar's ShearDegrees to slant its brackets, so the posture MID must carry the
	// same slant (written right after Bind).
	if (Img_HealthBar && HealthBarMaterial)
	{
		Img_HealthBar->SetBrushFromMaterial(HealthBarMaterial);
	}
	if (Img_HealthTrail && TrailBarMaterial)
	{
		Img_HealthTrail->SetBrushFromMaterial(TrailBarMaterial);
	}
	if (Img_PostureBar && PostureBarMaterial)
	{
		Img_PostureBar->SetBrushFromMaterial(PostureBarMaterial);
	}
	if (Img_Brackets && BracketsMaterial)
	{
		Img_Brackets->SetBrushFromMaterial(BracketsMaterial);
	}
	EnsureBarMaterials();

	UWidget* ScaleRoot = Row_Posture ? Row_Posture.Get() : (Img_PostureBar ? Cast<UWidget>(Img_PostureBar.Get()) : Cast<UWidget>(Bar_Posture.Get()));
	Presenter.Bind(Img_PostureBar, Img_Brackets, Img_PostureBar ? nullptr : Bar_Posture.Get(), ScaleRoot);
	Presenter.Reset();
	if (Img_PostureBar)
	{
		if (UMaterialInstanceDynamic* PostureMID = Img_PostureBar->GetDynamicMaterial())
		{
			PostureMID->SetScalarParameterValue(ShearDegreesParam, SlantDegrees);
		}
	}
	if (Bar_Posture && Img_PostureBar)
	{
		Bar_Posture->SetVisibility(ESlateVisibility::Collapsed);
	}

	bShown = false;
	CurrentOpacity = 0.f;
	SetRenderOpacity(0.f);
	SetVisibility(ESlateVisibility::Collapsed);
}

// ============================================================================
// Show / hide
// ============================================================================

void UBH_BossHealthBarWidget::PresentBoss(AActor* Boss)
{
	if (!Boss)
	{
		DismissBar();
		return;
	}

	const bool bNewBoss = BossActor.Get() != Boss;
	BossActor = Boss;

	UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Boss);
	if (!ASC)
	{
		ASC = Boss->FindComponentByClass<UAbilitySystemComponent>();
	}

	if (bNewBoss || !GetHUDAbilitySystem())
	{
		// A new boss starts from its own values: snap the fills instead of easing from the previous boss.
		bHealthSeeded = false;
		TrailHoldLeft = 0.f;
		Presenter.Reset();
		InitializeHUD(ASC);
	}

	if (Txt_Name)
	{
		const UBH_CombatIdentityComponent* Identity = UBH_CombatIdentityComponent::Find(Boss);
		ShownName = Identity ? Identity->GetBossBarName() : FText::FromString(Boss->GetName());
		Txt_Name->SetText(ShownName);
	}

	bShown = true;
	SetVisibility(ESlateVisibility::HitTestInvisible);
	EnsureActiveTimer();
}

void UBH_BossHealthBarWidget::DismissBar()
{
	bShown = false; // NativeTick fades out, then collapses and unbinds
}

// ============================================================================
// Health / posture
// ============================================================================

void UBH_BossHealthBarWidget::HandleHealthChanged(float Health, float MaxHealth)
{
	const float NewTarget = MaxHealth > 0.f ? FMath::Clamp(Health / MaxHealth, 0.f, 1.f) : 0.f;
	if (!bHealthSeeded)
	{
		DisplayedHealthPercent = NewTarget;
		TrailPercent = NewTarget;
		TrailHoldLeft = 0.f;
		bHealthSeeded = true;
	}
	else if (NewTarget < TargetHealthPercent - KINDA_SMALL_NUMBER)
	{
		// Damage: the trail keeps its value (it is never below the displayed health) and waits TrailDelay before catching up.
		TrailPercent = FMath::Max(TrailPercent, DisplayedHealthPercent);
		TrailHoldLeft = TrailDelay;
	}
	TargetHealthPercent = NewTarget;
	if (HealthEaseSpeed <= 0.f)
	{
		DisplayedHealthPercent = TargetHealthPercent;
	}
	ApplyBarPercents();
}

void UBH_BossHealthBarWidget::HandlePostureChanged(float Posture, float MaxPosture)
{
	// Same convention as UBH_TargetVitalsWidget: the fill grows as posture is lost.
	Presenter.SetDamageFraction(MaxPosture > 0.f ? 1.f - Posture / MaxPosture : 0.f);
}

void UBH_BossHealthBarWidget::HandlePostureBrokenChanged(bool bBroken)
{
	Presenter.SetBroken(bBroken);
}

void UBH_BossHealthBarWidget::ApplyBarPercents()
{
	EnsureBarMaterials();
	if (HealthBarMID)
	{
		HealthBarMID->SetScalarParameterValue(PercentParameterName, DisplayedHealthPercent);
	}
	if (TrailBarMID)
	{
		TrailBarMID->SetScalarParameterValue(PercentParameterName, TrailPercent);
	}
}

void UBH_BossHealthBarWidget::SyncAspectRatio(UImage* Image, UMaterialInstanceDynamic* MID, FVector2D& LastSize)
{
	// Keeps the slant true to the real pixel aspect (the material shears in UV space).
	const FVector2D Size = GetLocalSize(Image);
	if (MID && Size.X > 1.0 && Size.Y > 1.0 && !Size.Equals(LastSize, 0.01))
	{
		LastSize = Size;
		MID->SetScalarParameterValue(AspectRatioParam, static_cast<float>(Size.X / Size.Y));
	}
}

// ============================================================================
// Tick
// ============================================================================

void UBH_BossHealthBarWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	UpdateBar(InDeltaTime);
}

void UBH_BossHealthBarWidget::EnsureActiveTimer()
{
	if (!ActiveTimerHandle.IsValid())
	{
		ActiveTimerHandle = TakeWidget()->RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateUObject(this, &UBH_BossHealthBarWidget::OnActiveTimer));
	}
}

EActiveTimerReturnType UBH_BossHealthBarWidget::OnActiveTimer(double InCurrentTime, float InDeltaTime)
{
	UpdateBar(InDeltaTime);
	if (!bShown && CurrentOpacity <= 0.f && GetVisibility() == ESlateVisibility::Collapsed)
	{
		ActiveTimerHandle.Reset();
		return EActiveTimerReturnType::Stop;
	}
	return EActiveTimerReturnType::Continue;
}

void UBH_BossHealthBarWidget::UpdateBar(float InDeltaTime)
{
	if (LastUpdateFrame == GFrameCounter)
	{
		return;
	}
	LastUpdateFrame = GFrameCounter;

	// -- Fade: in over FadeInTime, out over FadeOutTime; fully out = collapsed and unbound.
	const float TargetOpacity = bShown ? 1.f : 0.f;
	if (!FMath::IsNearlyEqual(CurrentOpacity, TargetOpacity))
	{
		const float Duration = bShown ? FadeInTime : FadeOutTime;
		CurrentOpacity = FMath::FInterpConstantTo(CurrentOpacity, TargetOpacity, InDeltaTime, 1.f / FMath::Max(Duration, 0.01f));
		SetRenderOpacity(CurrentOpacity);
	}
	if (!bShown && CurrentOpacity <= 0.f)
	{
		if (GetVisibility() != ESlateVisibility::Collapsed)
		{
			SetVisibility(ESlateVisibility::Collapsed);
			InitializeHUD(nullptr);
			BossActor.Reset();
		}
		return;
	}

	// -- Health: eased fill, then the trail holds and catches up.
	bool bDirty = false;
	if (!FMath::IsNearlyEqual(DisplayedHealthPercent, TargetHealthPercent, 0.0005f))
	{
		DisplayedHealthPercent = HealthEaseSpeed > 0.f
			? FMath::FInterpTo(DisplayedHealthPercent, TargetHealthPercent, InDeltaTime, HealthEaseSpeed)
			: TargetHealthPercent;
		bDirty = true;
	}
	if (TrailHoldLeft > 0.f)
	{
		TrailHoldLeft -= InDeltaTime;
	}
	else if (TrailPercent > DisplayedHealthPercent)
	{
		TrailPercent = FMath::FInterpConstantTo(TrailPercent, DisplayedHealthPercent, InDeltaTime, TrailCatchUpSpeed);
		bDirty = true;
	}
	if (TrailPercent < DisplayedHealthPercent)
	{
		TrailPercent = DisplayedHealthPercent; // healing: the trail never sits below the fill
		bDirty = true;
	}
	if (bDirty)
	{
		ApplyBarPercents();
	}

	SyncAspectRatio(Img_HealthBar, HealthBarMID, LastHealthSize);
	SyncAspectRatio(Img_HealthTrail, TrailBarMID, LastTrailSize);

	Presenter.Tick(InDeltaTime, PostureStyle);
}
