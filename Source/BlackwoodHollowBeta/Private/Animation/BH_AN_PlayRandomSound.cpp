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
	if (bUseOwnerVoiceSet)
	{
		const UEnum* Enum = StaticEnum<EBH_VoiceCategory>();
		return FString::Printf(TEXT("Voice %s (%.0f%%)"), Enum ? *Enum->GetNameStringByValue(static_cast<int64>(VoiceCategory)) : TEXT("?"), Chance * 100.f);
	}
	return FString::Printf(TEXT("Random Sound (%d)"), Sounds.Num());
}

void UBH_AN_PlayRandomSound::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!MeshComp)
	{
		return;
	}

	if (bUseOwnerVoiceSet)
	{
		UBH_CombatFeelLibrary::PlayVoice(MeshComp->GetOwner(), VoiceCategory, Chance);
		return;
	}

	if (Chance < 1.f && FMath::FRand() > Chance)
	{
		return;
	}

	const bool bUseSocket = !AttachSocketName.IsNone() && MeshComp->DoesSocketExist(AttachSocketName);
	const FVector Location = bUseSocket ? MeshComp->GetSocketLocation(AttachSocketName) : MeshComp->GetComponentLocation();

	BH_CueUtils::PlayRandomSound(MeshComp, Sounds, Location, VolumeRange, PitchRange, Attenuation);
}
