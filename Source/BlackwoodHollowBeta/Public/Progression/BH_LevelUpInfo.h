// Blackwood Hollow - level-up details (Phase 9)
// Target: Unreal Engine 5.8 (C++)
//
// What changed when a player gained a level. Computed on the SERVER by UBH_ProgressionComponent (player curve rows evaluated at the
// old and the new level) and sent to the owning client, which hands it to the HUD (UBH_HUDSubsystem::ShowLevelUpDetailed ->
// UBH_LevelUpBannerWidget). Gains are the deltas of the BASE max stats from the CT_PlayerScaling curves (equipment is not included).
// A missing curve row gives a gain of 0.

#pragma once

#include "CoreMinimal.h"
#include "BH_LevelUpInfo.generated.h"

USTRUCT(BlueprintType)
struct BLACKWOODHOLLOWBETA_API FBH_LevelUpInfo
{
	GENERATED_BODY()

	/** The level reached (when several levels are gained at once: the final level). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|Progression")
	int32 NewLevel = 1;

	/** The level before the gain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|Progression")
	int32 PreviousLevel = 1;

	/** Increase of the base MaxHealth (curve at NewLevel minus curve at PreviousLevel). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|Progression")
	float HealthGain = 0.f;

	/** Increase of the base MaxPosture. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|Progression")
	float PostureGain = 0.f;

	/** Increase of the base MaxStamina. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|Progression")
	float StaminaGain = 0.f;
};
