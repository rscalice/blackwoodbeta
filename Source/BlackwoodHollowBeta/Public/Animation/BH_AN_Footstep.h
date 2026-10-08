// Blackwood Hollow - Phase 10A AnimNotify that plays a surface-aware footstep (replaces the GASP foley notifies)

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Audio/BH_FootstepTypes.h"
#include "BH_AN_Footstep.generated.h"

class UBH_FootstepSet;

/**
 * Traces the ground under the foot (ECC_Visibility, ignoring the owner), reads the hit physical material's surface and plays
 * that surface's sound for Event at the hit point, plus the armor layer for the owner's heaviest armor weight class
 * (State.Armor.Weight.Heavy / .Medium tags on the owner's ASC; no tag = Cloth) and the surface's optional ImpactFX.
 *
 * Sounds, volumes, trace distances and the retrigger guard come from the FootstepSet (this notify's override, else
 * UBH_RPGSettings::DefaultFootstepSet). Event = Combat multiplies the volume by the set's CombatVolumeMultiplier (0.33).
 *
 * Cosmetic only: fires on every machine, replicates nothing, and is skipped on dedicated servers, in previews without an
 * owner, and while the source (montage / sequence) is blending out. The same foot of the same mesh cannot retrigger within
 * the set's MinRetriggerSeconds. Soft sound / FX refs are loaded with LoadSynchronous (small one-shots).
 */
UCLASS(meta = (DisplayName = "Footstep (Surface)"))
class BLACKWOODHOLLOWBETA_API UBH_AN_Footstep : public UAnimNotify
{
	GENERATED_BODY()

public:
	UBH_AN_Footstep();

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footstep")
	EBH_FootstepFoot Foot = EBH_FootstepFoot::Left;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footstep")
	EBH_FootstepEvent Event = EBH_FootstepEvent::Walk;

	/** Extra volume scale for this one notify. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footstep", meta = (ClampMin = "0.0"))
	float VolumeMultiplier = 1.f;

	/** Optional override; None = UBH_RPGSettings::DefaultFootstepSet. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footstep")
	TObjectPtr<UBH_FootstepSet> FootstepSet;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footstep")
	FName LeftFootBone = TEXT("foot_l");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footstep")
	FName RightFootBone = TEXT("foot_r");
};
