// Blackwood Hollow - Code-only camera shakes (implementation)

#include "Cues/BH_CameraShakes.h"
#include "Shakes/WaveOscillatorCameraShakePattern.h"

namespace BH_CameraShakes_Private
{
	static void Configure(UWaveOscillatorCameraShakePattern* Pattern, float Duration, float BlendIn, float BlendOut,
		float PitchAmp, float YawAmp, float RollAmp, float Frequency)
	{
		Pattern->Duration = Duration;
		Pattern->BlendInTime = BlendIn;
		Pattern->BlendOutTime = BlendOut;

		Pattern->Pitch.Amplitude = PitchAmp;
		Pattern->Pitch.Frequency = Frequency;
		Pattern->Pitch.InitialOffsetType = EInitialWaveOscillatorOffsetType::Zero;

		Pattern->Yaw.Amplitude = YawAmp;
		Pattern->Yaw.Frequency = Frequency * 0.8f;
		Pattern->Yaw.InitialOffsetType = EInitialWaveOscillatorOffsetType::Zero;

		Pattern->Roll.Amplitude = RollAmp;
		Pattern->Roll.Frequency = Frequency * 1.2f;
		Pattern->Roll.InitialOffsetType = EInitialWaveOscillatorOffsetType::Zero;

		// Rotation only: no location / FOV wobble.
		Pattern->X.Amplitude = 0.f;
		Pattern->Y.Amplitude = 0.f;
		Pattern->Z.Amplitude = 0.f;
		Pattern->FOV.Amplitude = 0.f;
	}
}

UBH_CameraShake_Hit::UBH_CameraShake_Hit(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	UWaveOscillatorCameraShakePattern* Pattern = CreateDefaultSubobject<UWaveOscillatorCameraShakePattern>(TEXT("RootShakePattern"));
	BH_CameraShakes_Private::Configure(Pattern, /*Duration*/ 0.15f, /*In*/ 0.02f, /*Out*/ 0.10f,
		/*Pitch*/ 1.2f, /*Yaw*/ 0.6f, /*Roll*/ 0.4f, /*Hz*/ 28.f);
	SetRootShakePattern(Pattern);
}

UBH_CameraShake_ParryPunch::UBH_CameraShake_ParryPunch(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	UWaveOscillatorCameraShakePattern* Pattern = CreateDefaultSubobject<UWaveOscillatorCameraShakePattern>(TEXT("RootShakePattern"));
	BH_CameraShakes_Private::Configure(Pattern, /*Duration*/ 0.25f, /*In*/ 0.02f, /*Out*/ 0.18f,
		/*Pitch*/ 2.8f, /*Yaw*/ 1.6f, /*Roll*/ 1.2f, /*Hz*/ 24.f);
	SetRootShakePattern(Pattern);
}
