// Blackwood Hollow - Code-only camera shakes used by the combat GameplayCues
// (no assets needed; tweak by subclassing in Blueprint if desired).

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraShakeBase.h"
#include "BH_CameraShakes.generated.h"

/** Short, small rotational jolt for a connecting melee hit (0.15 s). */
UCLASS()
class BLACKWOODHOLLOWBETA_API UBH_CameraShake_Hit : public UCameraShakeBase
{
	GENERATED_BODY()

public:
	UBH_CameraShake_Hit(const FObjectInitializer& ObjectInitializer);
};

/** Heavier punch for a successful parry (0.25 s). */
UCLASS()
class BLACKWOODHOLLOWBETA_API UBH_CameraShake_ParryPunch : public UCameraShakeBase
{
	GENERATED_BODY()

public:
	UBH_CameraShake_ParryPunch(const FObjectInitializer& ObjectInitializer);
};

// ---- Tiered impact shakes (UBH_CombatFeelLibrary::PlayImpactFeel picks one per EBH_ImpactTier; see BH_CombatFeel.h) ----

/** Tier Light: tiny tick for a weak hit (0.12 s). */
UCLASS()
class BLACKWOODHOLLOWBETA_API UBH_CameraShake_Light : public UCameraShakeBase
{
	GENERATED_BODY()

public:
	UBH_CameraShake_Light(const FObjectInitializer& ObjectInitializer);
};

/** Tier Medium: a solid hit (0.2 s). */
UCLASS()
class BLACKWOODHOLLOWBETA_API UBH_CameraShake_Medium : public UCameraShakeBase
{
	GENERATED_BODY()

public:
	UBH_CameraShake_Medium(const FObjectInitializer& ObjectInitializer);
};

/** Tier Heavy: bigger rotational kick with a small FOV punch (0.3 s). */
UCLASS()
class BLACKWOODHOLLOWBETA_API UBH_CameraShake_Heavy : public UCameraShakeBase
{
	GENERATED_BODY()

public:
	UBH_CameraShake_Heavy(const FObjectInitializer& ObjectInitializer);
};

// ---- Directional bias shakes (UBH_CombatFeelLibrary::PlayDirectionalBiasShake picks one by which side of the camera the hit comes from) ----

/** Hit coming from the camera's LEFT: head kicks right (positive yaw / roll) with a small pitch (0.2 s). */
UCLASS()
class BLACKWOODHOLLOWBETA_API UBH_CameraShake_HitFromLeft : public UCameraShakeBase
{
	GENERATED_BODY()

public:
	UBH_CameraShake_HitFromLeft(const FObjectInitializer& ObjectInitializer);
};

/** Hit coming from the camera's RIGHT: head kicks left (negative yaw / roll) with a small pitch (0.2 s). */
UCLASS()
class BLACKWOODHOLLOWBETA_API UBH_CameraShake_HitFromRight : public UCameraShakeBase
{
	GENERATED_BODY()

public:
	UBH_CameraShake_HitFromRight(const FObjectInitializer& ObjectInitializer);
};

/** Hit coming from in front of (or behind) the camera: a pitch kick with almost no roll or yaw (0.2 s). */
UCLASS()
class BLACKWOODHOLLOWBETA_API UBH_CameraShake_HitFromFront : public UCameraShakeBase
{
	GENERATED_BODY()

public:
	UBH_CameraShake_HitFromFront(const FObjectInitializer& ObjectInitializer);
};

/**
 * Parry FOV punch: one half sine of FOV, 0.25 s long, amplitude -1 degree. Start it with Scale = the kick in degrees
 * (6 -> the FOV narrows by 6 degrees at 0.125 s and eases back to normal at 0.25 s).
 */
UCLASS()
class BLACKWOODHOLLOWBETA_API UBH_CameraShake_ParryFOV : public UCameraShakeBase
{
	GENERATED_BODY()

public:
	UBH_CameraShake_ParryFOV(const FObjectInitializer& ObjectInitializer);
};

/** Tier Massive: posture break. Low-frequency rumble, a directional forward kick (play-space X) and FOV punch (0.5 s). */
UCLASS()
class BLACKWOODHOLLOWBETA_API UBH_CameraShake_Massive : public UCameraShakeBase
{
	GENERATED_BODY()

public:
	UBH_CameraShake_Massive(const FObjectInitializer& ObjectInitializer);
};
