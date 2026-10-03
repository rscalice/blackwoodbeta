// Blackwood Hollow - AnimNotify that moves the equipped weapons between the hand and their sheathed socket

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "BH_AN_WeaponAttach.generated.h"

/**
 * Placed on the draw / sheath montages at the grab / release frame. Fires on every machine that plays the montage
 * (the transition montage is played locally by UBH_StanceComponent). bToHand = true attaches the weapons to their hand
 * sockets, false to their sheathed sockets (FBH_WeaponMeshSlot::SheathedSocket; weapons without one stay in the hand).
 */
UCLASS(meta = (DisplayName = "BH Weapon Attach"))
class BLACKWOODHOLLOWBETA_API UBH_AN_WeaponAttach : public UAnimNotify
{
	GENERATED_BODY()

public:
	UBH_AN_WeaponAttach();

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;

	/** true: weapons go to the hand; false: weapons go to the sheathed socket. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon")
	bool bToHand = true;
};
