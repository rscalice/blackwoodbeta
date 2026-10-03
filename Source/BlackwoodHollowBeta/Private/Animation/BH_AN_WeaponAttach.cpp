// Blackwood Hollow - AnimNotify that moves the equipped weapons between the hand and their sheathed socket (implementation)

#include "Animation/BH_AN_WeaponAttach.h"
#include "Combat/BH_StanceComponent.h"
#include "Components/SkeletalMeshComponent.h"

UBH_AN_WeaponAttach::UBH_AN_WeaponAttach()
{
#if WITH_EDITORONLY_DATA
	NotifyColor = FColor(255, 190, 90);
#endif
}

FString UBH_AN_WeaponAttach::GetNotifyName_Implementation() const
{
	return bToHand ? TEXT("Weapon To Hand") : TEXT("Weapon To Sheath");
}

void UBH_AN_WeaponAttach::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	if (!MeshComp)
	{
		return;
	}
	if (UBH_StanceComponent* Stance = UBH_StanceComponent::FindStanceComponent(MeshComp->GetOwner()))
	{
		Stance->HandleWeaponAttachNotify(bToHand);
	}
}
