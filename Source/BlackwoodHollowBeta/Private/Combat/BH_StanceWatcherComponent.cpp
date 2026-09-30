// Blackwood Hollow - stance (GASP overlay pose) watcher (implementation)

#include "Combat/BH_StanceWatcherComponent.h"
#include "Combat/BH_WeaponLoadoutDataAsset.h"
#include "AbilitySystem/BH_CombatFunctionLibrary.h"
#include "GameFramework/Character.h"
#include "Components/MeshComponent.h"
#include "UObject/UnrealType.h"

UBH_StanceWatcherComponent::UBH_StanceWatcherComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickInterval = PollInterval;
	SetIsReplicatedByDefault(true);
}

UBH_StanceWatcherComponent* UBH_StanceWatcherComponent::FindStanceWatcher(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UBH_StanceWatcherComponent>() : nullptr;
}

void UBH_StanceWatcherComponent::BeginPlay()
{
	Super::BeginPlay();
	PrimaryComponentTick.TickInterval = PollInterval;
}

void UBH_StanceWatcherComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	SyncWeapons(false);
}

const UBH_WeaponLoadoutDataAsset* UBH_StanceWatcherComponent::ResolveLoadouts() const
{
	if (const AActor* Owner = GetOwner())
	{
		if (const FObjectProperty* Property = CastField<FObjectProperty>(Owner->GetClass()->FindPropertyByName(FName(TEXT("WeaponLoadouts")))))
		{
			if (const UBH_WeaponLoadoutDataAsset* FromCharacter = Cast<UBH_WeaponLoadoutDataAsset>(Property->GetObjectPropertyValue_InContainer(Owner)))
			{
				return FromCharacter;
			}
		}
	}
	return FallbackLoadouts;
}

void UBH_StanceWatcherComponent::SyncWeapons(bool bForce)
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character)
	{
		return;
	}

	const FString Pose = UBH_CombatFunctionLibrary::GetCurrentOverlayPoseDisplayName(Character);
	if (Pose.IsEmpty() || (!bForce && Pose == LastSyncedPose))
	{
		return;
	}

	const UBH_WeaponLoadoutDataAsset* Loadouts = ResolveLoadouts();
	if (!Loadouts)
	{
		return; // variable not ready yet; retry next poll
	}

	// First observation: the character's own setup may already have equipped this pose. Adopt it without a re-attach.
	if (LastSyncedPose.IsEmpty() && !bForce && UBH_CombatFunctionLibrary::GetEquippedWeaponComponent(Character, EBH_WeaponSlot::MainHand))
	{
		LastSyncedPose = Pose;
		return;
	}

	LastSyncedPose = Pose;
	TArray<UMeshComponent*> Attached;
	UBH_CombatFunctionLibrary::EquipWeaponsForOverlayPose(Character, Loadouts, Pose, Attached);
}

void UBH_StanceWatcherComponent::RequestStance(const FString& StanceDisplayName)
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}
	if (Owner->HasAuthority())
	{
		UBH_CombatFunctionLibrary::ApplyOverlayPoseByDisplayName(Owner, StanceDisplayName);
	}
	else
	{
		Server_SetStance(StanceDisplayName);
	}
}

void UBH_StanceWatcherComponent::Server_SetStance_Implementation(const FString& StanceDisplayName)
{
	if (AActor* Owner = GetOwner())
	{
		UBH_CombatFunctionLibrary::ApplyOverlayPoseByDisplayName(Owner, StanceDisplayName);
	}
}
