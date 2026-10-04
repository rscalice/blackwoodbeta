// Blackwood Hollow - Code-only camera shakes (implementation)
// Tier shakes (Light..Massive): durations are x1.3 of the original; amplitude escalation is DA_CombatFeel Tiers[].ShakeScale (x2.5).

#include "Cues/BH_CameraShakes.h"
#include "Shakes/WaveOscillatorCameraShakePattern.h"

namespace BH_CameraShakes_Private
{
	static void Configure(UWaveOscillatorCameraShakePattern* Pattern, float Duration, float BlendIn, float BlendOut,
		float PitchAmp, float YawAmp, float RollAmp, float Frequency, float FOVAmp = 0.f, float KickXAmp = 0.f, float KickZAmp = 0.f)
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
		Pattern->X.Amplitude = KickXAmp; // along the play space's X: the impact direction for UserDefined play space
		Pattern->X.Frequency = Frequency * 0.5f;
		Pattern->X.InitialOffsetType = EInitialWaveOscillatorOffsetType::Zero;
		Pattern->Y.Amplitude = 0.f;
		Pattern->Z.Amplitude = KickZAmp;
		Pattern->Z.Frequency = Frequency * 0.7f;
		Pattern->Z.InitialOffsetType = EInitialWaveOscillatorOffsetType::Zero;
		Pattern->FOV.Amplitude = FOVAmp;
		Pattern->FOV.Frequency = Frequency * 0.6f;
		Pattern->FOV.InitialOffsetType = EInitialWaveOscillatorOffsetType::Zero;
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

UBH_CameraShake_Light::UBH_CameraShake_Light(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	UWaveOscillatorCameraShakePattern* Pattern = CreateDefaultSubobject<UWaveOscillatorCameraShakePattern>(TEXT("RootShakePattern"));
	BH_CameraShakes_Private::Configure(Pattern, /*Duration*/ 0.156f, /*In*/ 0.01f, /*Out*/ 0.104f,
		/*Pitch*/ 0.6f, /*Yaw*/ 0.3f, /*Roll*/ 0.2f, /*Hz*/ 30.f);
	SetRootShakePattern(Pattern);
}

UBH_CameraShake_Medium::UBH_CameraShake_Medium(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	UWaveOscillatorCameraShakePattern* Pattern = CreateDefaultSubobject<UWaveOscillatorCameraShakePattern>(TEXT("RootShakePattern"));
	BH_CameraShakes_Private::Configure(Pattern, /*Duration*/ 0.26f, /*In*/ 0.015f, /*Out*/ 0.182f,
		/*Pitch*/ 1.5f, /*Yaw*/ 0.8f, /*Roll*/ 0.6f, /*Hz*/ 26.f);
	SetRootShakePattern(Pattern);
}

UBH_CameraShake_Heavy::UBH_CameraShake_Heavy(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	UWaveOscillatorCameraShakePattern* Pattern = CreateDefaultSubobject<UWaveOscillatorCameraShakePattern>(TEXT("RootShakePattern"));
	BH_CameraShakes_Private::Configure(Pattern, /*Duration*/ 0.39f, /*In*/ 0.02f, /*Out*/ 0.26f,
		/*Pitch*/ 3.4f, /*Yaw*/ 1.9f, /*Roll*/ 1.8f, /*Hz*/ 22.f, /*FOV*/ 1.5f, /*KickX*/ 2.0f, /*KickZ*/ 0.f);
	SetRootShakePattern(Pattern);
}

UBH_CameraShake_Massive::UBH_CameraShake_Massive(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	UWaveOscillatorCameraShakePattern* Pattern = CreateDefaultSubobject<UWaveOscillatorCameraShakePattern>(TEXT("RootShakePattern"));
	BH_CameraShakes_Private::Configure(Pattern, /*Duration*/ 0.65f, /*In*/ 0.02f, /*Out*/ 0.455f,
		/*Pitch*/ 5.5f, /*Yaw*/ 3.2f, /*Roll*/ 3.6f, /*Hz*/ 11.f, /*FOV*/ 3.5f, /*KickX*/ 7.0f, /*KickZ*/ 2.0f);
	SetRootShakePattern(Pattern);
}
