// Blackwood Hollow - AnimNotify that plays a random sound from a list (sword-swing whooshes, footsteps, ...)

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Combat/BH_CombatFeel.h"
#include "BH_AN_PlayRandomSound.generated.h"

class USoundBase;
class USoundAttenuation;

/**
 * Picks one of Sounds at random (never the same twice in a row) and plays it at the mesh, or at AttachSocketName
 * if that socket exists. Cosmetic only: skipped on dedicated servers.
 *
 * Chance gates the whole notify (0.6 on light combo steps so efforts are not repetitive). With bUseOwnerVoiceSet the
 * sound comes from the owning character's voice set (UBH_CombatIdentityComponent::VoiceSet, or the player's default on
 * DA_CombatFeel) for VoiceCategory, with that set's pitch multiplier, and Sounds is ignored.
 */
UCLASS(meta = (DisplayName = "Play Random Sound"))
class BLACKWOODHOLLOWBETA_API UBH_AN_PlayRandomSound : public UAnimNotify
{
	GENERATED_BODY()

public:
	UBH_AN_PlayRandomSound();

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound")
	TArray<TObjectPtr<USoundBase>> Sounds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound")
	FVector2D VolumeRange = FVector2D(0.85, 1.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound")
	FVector2D PitchRange = FVector2D(0.95, 1.05);

	/** Optional socket/bone on the mesh to play from (e.g. the weapon hand). None or missing = mesh origin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound")
	FName AttachSocketName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound")
	TObjectPtr<USoundAttenuation> Attenuation;

	/** Probability (0..1) that this notify plays anything. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sound", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Chance = 1.f;

	/** Resolve the sounds from the owner's voice set instead of Sounds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice")
	bool bUseOwnerVoiceSet = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice", meta = (EditCondition = "bUseOwnerVoiceSet"))
	EBH_VoiceCategory VoiceCategory = EBH_VoiceCategory::AttackLight;
};
