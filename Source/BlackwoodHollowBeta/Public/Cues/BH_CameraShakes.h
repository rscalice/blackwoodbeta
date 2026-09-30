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
