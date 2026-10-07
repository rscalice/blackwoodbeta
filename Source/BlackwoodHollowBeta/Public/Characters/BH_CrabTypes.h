// Blackwood Hollow - crab enemy shared types
// Target: Unreal Engine 5.8 (C++)

#pragma once

#include "CoreMinimal.h"
#include "BH_CrabTypes.generated.h"

/**
 * What the crab is doing right now, as far as its animation is concerned. Replicated on ABH_EnemyCrab (the GAS loose tags
 * that drive some of these do not replicate to clients) and read by UBH_CrabAnimInstance. The ABP picks the pose from it.
 */
UENUM(BlueprintType)
enum class EBH_CrabActionPhase : uint8
{
	Idle,
	JabWindup,
	JabStrike,
	PinchWindup,
	PinchSnap,
	Sidestep,
	HitReact,
	Stagger,
	Dead
};
