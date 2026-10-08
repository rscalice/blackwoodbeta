// Blackwood Hollow - Phase 10A footstep enums (foot + event type)

#pragma once

#include "CoreMinimal.h"
#include "BH_FootstepTypes.generated.h"

/** Which foot a footstep notify belongs to. Root = no single foot (land / jump / roll): played from the mesh origin. */
UENUM(BlueprintType)
enum class EBH_FootstepFoot : uint8
{
	Left,
	Right,
	Root
};

/**
 * Footstep event type. Covers every GASP foley event (Walk, Run, Land, Scuff, Jump, Handplant, Roll, Crouch) plus our
 * own Sprint and Combat (steps fired from combat montages, played at UBH_FootstepSet::CombatVolumeMultiplier).
 */
UENUM(BlueprintType)
enum class EBH_FootstepEvent : uint8
{
	Walk,
	Run,
	Sprint,
	Crouch,
	Land,
	Scuff,
	Jump,
	Handplant,
	Roll,
	Combat
};
