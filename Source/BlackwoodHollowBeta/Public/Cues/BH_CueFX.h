// Blackwood Hollow - reusable cosmetic FX block for GameplayCue notifies (Niagara system + random sound)

#pragma once

#include "CoreMinimal.h"
#include "BH_CueFX.generated.h"

class UNiagaraSystem;
class USoundBase;

/**
 * One visual + audio effect. Every combat cue exposes one or more of these so designers tune them the same way
 * (played through BH_CueUtils::PlayCueFX).
 */
USTRUCT(BlueprintType)
struct BLACKWOODHOLLOWBETA_API FBH_CueFX
{
	GENERATED_BODY()

	/** Niagara system to spawn (null = no visual). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FX")
	TObjectPtr<UNiagaraSystem> System = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FX")
	FVector Scale = FVector::OneVector;

	/** Seconds after which the system is deactivated (guards looping systems). 0 = let the system finish on its own. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FX", meta = (ClampMin = "0.0"))
	float MaxLifetime = 0.f;

	/** World-space offset added to the cue's spawn location (for both system and sound). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FX")
	FVector Offset = FVector::ZeroVector;

	/** One entry is picked at random per play (never the same twice in a row when there are 2+). Empty = silent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FX")
	TArray<TObjectPtr<USoundBase>> Sounds;

	/** Random volume multiplier range (X = min, Y = max). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FX")
	FVector2D VolumeRange = FVector2D(0.9, 1.0);

	/** Random pitch multiplier range (X = min, Y = max). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FX")
	FVector2D PitchRange = FVector2D(0.95, 1.05);
};
