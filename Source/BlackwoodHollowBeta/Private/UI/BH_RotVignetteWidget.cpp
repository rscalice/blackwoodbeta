// Blackwood Hollow - full-screen Blight Rot vignette (implementation)

#include "UI/BH_RotVignetteWidget.h"
#include "Components/Image.h"
#include "Materials/MaterialInstanceDynamic.h"

void UBH_RotVignetteWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (IsDesignTime())
	{
		return;
	}

	Intensity = 0.f;
	SetVisibility(ESlateVisibility::Collapsed);
}

void UBH_RotVignetteWidget::SetIntensity(float NewIntensity)
{
	const float Clamped = FMath::Clamp(NewIntensity, 0.f, 1.f);
	if (FMath::IsNearlyEqual(Clamped, Intensity, 0.0005f) && !(Clamped == 0.f && Intensity != 0.f))
	{
		return;
	}
	Intensity = Clamped;

	if (Img_Vignette)
	{
		if (!VignetteMaterial)
		{
			VignetteMaterial = Img_Vignette->GetDynamicMaterial();
		}
		if (VignetteMaterial)
		{
			VignetteMaterial->SetScalarParameterValue(IntensityParameterName, Intensity);
		}
		if (bDriveRenderOpacity)
		{
			Img_Vignette->SetRenderOpacity(Intensity);
		}
	}

	SetVisibility(Intensity > 0.f ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	OnIntensityChanged(Intensity);
}
