// Blackwood Hollow - AnimNotify that plays a random sound from a list (sword-swing whooshes, footsteps, ...)

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "BH_AN_PlayRandomSound.generated.h"

class USoundBase;
class USoundAttenuation;

/**
 * Picks one of Sounds at random (never the same twice in a row) and plays it at the mesh, or at AttachSocketName
 * if that socket exists. Cosmetic only: skipped on dedicated servers.
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
};
