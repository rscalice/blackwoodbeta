// Blackwood Hollow - post-animation two-hand weapon grip-frame driver
// Target: Unreal Engine 5.8 (C++)

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BH_TwoHandAimComponent.generated.h"

/**
 * Ticks in TG_PostUpdateWork (after the character's skeletal meshes have evaluated this frame) and calls
 * UBH_CombatFunctionLibrary::ApplyTwoHandAim. Spawned at runtime by AttachWeaponMesh for two-handed main-hand weapons.
 */
UCLASS(ClassGroup = (BlackwoodHollow))
class BLACKWOODHOLLOWBETA_API UBH_TwoHandAimComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBH_TwoHandAimComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	float SmoothedWeight = 0.f;
	FQuat PrevRotation = FQuat::Identity;
	bool bHasPrevRotation = false;
};
