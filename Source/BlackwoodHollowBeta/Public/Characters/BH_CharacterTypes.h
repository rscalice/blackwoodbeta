// Blackwood Hollow - shared character enums for the motion-matching character base

#pragma once

#include "CoreMinimal.h"
#include "BH_CharacterTypes.generated.h"

/** Mirrors /Game/GASP/Blueprints/Data/E_Gait. The order MUST match the GASP enum. */
UENUM(BlueprintType)
enum class EBH_Gait : uint8
{
	Walk = 0,
	Run = 1,
	Sprint = 2,
};

/** Derived from the movement component flags, never stored. */
UENUM(BlueprintType)
enum class EBH_RotationMode : uint8
{
	OrientToMovement = 0,
	Strafe = 1,
};
