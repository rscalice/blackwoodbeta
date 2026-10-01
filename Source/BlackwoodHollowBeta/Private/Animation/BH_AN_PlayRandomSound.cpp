// Blackwood Hollow - AnimNotify that plays a random sound from a list (implementation)

#include "Animation/BH_AN_PlayRandomSound.h"
#include "Cues/BH_CueUtils.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Sound/SoundBase.h"

UBH_AN_PlayRandomSound::UBH_AN_PlayRandomSound()
{
#if WITH_EDITORONLY_DATA
	NotifyColor = FColor(120, 200, 255);
#endif
}

FString UBH_AN_PlayRandomSound::GetNotifyName_Implementation() const
{
	return FString::Printf(TEXT("Random Sound (%d)"), Sounds.Num());
}

void UBH_AN_PlayRandomSound::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!MeshComp)
	{
		return;
	}

	const bool bUseSocket = !AttachSocketName.IsNone() && MeshComp->DoesSocketExist(AttachSocketName);
	const FVector Location = bUseSocket ? MeshComp->GetSocketLocation(AttachSocketName) : MeshComp->GetComponentLocation();

	BH_CueUtils::PlayRandomSound(MeshComp, Sounds, Location, VolumeRange, PitchRange, Attenuation);
}
