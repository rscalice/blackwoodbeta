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

/** Tier Massive: posture break. Low-frequency rumble, a directional forward kick (play-space X) and FOV punch (0.5 s). */
UCLASS()
class BLACKWOODHOLLOWBETA_API UBH_CameraShake_Massive : public UCameraShakeBase
{
	GENERATED_BODY()

public:
	UBH_CameraShake_Massive(const FObjectInitializer& ObjectInitializer);
};
