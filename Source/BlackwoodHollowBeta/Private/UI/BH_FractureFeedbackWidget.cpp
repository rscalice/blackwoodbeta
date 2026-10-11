// Blackwood Hollow - Fracture local feedback widget (implementation)

#include "UI/BH_FractureFeedbackWidget.h"
#include "AbilitySystem/StatusEffects/BH_FractureEffects.h"
#include "AbilitySystem/StatusEffects/BH_StatusEffect.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"

void UBH_FractureFeedbackWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (IsDesignTime())
	{
		return;
	}

	bFractureActive = false;
	bFirstPollDone = false;
	CrackAmount = 0.f;
	PollTimer = 0.f;
	CrackMaterial = nullptr;

	if (Img_Cracks)
	{
		// The brush must be M_UI_FractureCracks (or an instance of it); GetDynamicMaterial returns null for a plain texture brush.
		CrackMaterial = Img_Cracks->GetDynamicMaterial();
		ApplyCrackAmount();
	}
	if (Img_FractureIcon) { Img_FractureIcon->SetVisibility(ESlateVisibility::Collapsed); }
	if (Txt_FractureName) { Txt_FractureName->SetVisibility(ESlateVisibility::Collapsed); }

	PollFracture();
}

void UBH_FractureFeedbackWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	PollTimer += InDeltaTime;
	if (PollTimer >= PollInterval)
	{
		PollTimer = 0.f;
		PollFracture();
	}

	// Ease the crack amount towards its target (linear: a full 0 -> CrackIntensity swing takes the fade time).
	const float Target = bFractureActive ? FMath::Clamp(CrackIntensity, 0.f, 1.f) : 0.f;
	if (!FMath::IsNearlyEqual(CrackAmount, Target, 0.0001f))
	{
		const float FadeSeconds = Target > CrackAmount ? CrackFadeInSeconds : CrackFadeOutSeconds;
		if (FadeSeconds > KINDA_SMALL_NUMBER)
		{
			const float Speed = FMath::Max(CrackIntensity, 0.01f) / FadeSeconds;
			CrackAmount = FMath::FInterpConstantTo(CrackAmount, Target, InDeltaTime, Speed);
		}
		else
		{
			CrackAmount = Target;
		}
		ApplyCrackAmount();
	}
}

void UBH_FractureFeedbackWidget::PollFracture()
{
	// The pawn may not exist yet or may be replaced on respawn: look it up every time.
	const APlayerController* PC = GetOwningPlayer();
	const UAbilitySystemComponent* ASC = PC ? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(PC->GetPawn()) : nullptr;
	const bool bNow = UBH_FractureLibrary::IsFractured(ASC);

	if (bNow != bFractureActive || !bFirstPollDone)
	{
		bFirstPollDone = true;
		SetFractureActive(bNow);
	}
}

void UBH_FractureFeedbackWidget::SetFractureActive(bool bActive)
{
	bFractureActive = bActive;

	if (Img_FractureIcon || Txt_FractureName)
	{
		FBH_ActiveStatusEffect Effect;
		bool bHaveEffect = false;
		if (bActive)
		{
			const APlayerController* PC = GetOwningPlayer();
			const UAbilitySystemComponent* ASC = PC ? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(PC->GetPawn()) : nullptr;
			bHaveEffect = ASC && UBH_StatusEffectLibrary::GetStatusEffect(ASC, TAG_State_Status_Fracture, Effect);
		}

		if (Img_FractureIcon)
		{
			UTexture2D* Icon = bHaveEffect ? Effect.Icon.LoadSynchronous() : nullptr;
			if (!Icon)
			{
				Icon = FallbackIcon;
			}
			if (bActive && Icon)
			{
				Img_FractureIcon->SetBrushFromTexture(Icon);
				Img_FractureIcon->SetVisibility(ESlateVisibility::HitTestInvisible);
			}
			else
			{
				Img_FractureIcon->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
		if (Txt_FractureName)
		{
			if (bActive)
			{
				Txt_FractureName->SetText(bHaveEffect && !Effect.DisplayName.IsEmpty() ? Effect.DisplayName : NSLOCTEXT("BlackwoodHollow", "FractureHudName", "Fracture"));
				Txt_FractureName->SetVisibility(ESlateVisibility::HitTestInvisible);
			}
			else
			{
				Txt_FractureName->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
	}

	OnFractureChanged(bActive);
}

void UBH_FractureFeedbackWidget::ApplyCrackAmount()
{
	if (!Img_Cracks)
	{
		return;
	}
	if (CrackMaterial)
	{
		CrackMaterial->SetScalarParameterValue(CrackParameterName, CrackAmount);
	}
	Img_Cracks->SetVisibility(CrackAmount > KINDA_SMALL_NUMBER ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}
