// Blackwood Hollow - post-animation two-hand weapon aim driver

#include "Combat/BH_TwoHandAimComponent.h"

#include "AbilitySystem/BH_CombatFunctionLibrary.h"
#include "GameFramework/Character.h"

UBH_TwoHandAimComponent::UBH_TwoHandAimComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UBH_TwoHandAimComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UBH_CombatFunctionLibrary::ApplyTwoHandAim(Cast<ACharacter>(GetOwner()), DeltaTime, SmoothedWeight, PrevRotation, bHasPrevRotation);
}
