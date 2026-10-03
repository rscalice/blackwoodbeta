// Blackwood Hollow - per-stance movement profile (implementation)

#include "Characters/BH_StanceMovementProfile.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#endif

const FBH_GaitMovementSettings& UBH_StanceMovementProfile::GetGaitSettings(EBH_Gait Gait) const
{
	switch (Gait)
	{
	case EBH_Gait::Walk:
		return Walk;
	case EBH_Gait::Sprint:
		return Sprint;
	default:
		return Run;
	}
}

float UBH_StanceMovementProfile::GetMaxAccelerationFor(EBH_Gait Gait, float Speed2D) const
{
	if (Gait == EBH_Gait::Sprint)
	{
		return FMath::GetMappedRangeValueClamped(SprintAccelSpeedRange, SprintAccelOutput, Speed2D);
	}
	return GetGaitSettings(Gait).MaxAcceleration;
}

float UBH_StanceMovementProfile::GetGroundFrictionFor(EBH_Gait Gait, float Speed2D) const
{
	if (Gait == EBH_Gait::Sprint)
	{
		return FMath::GetMappedRangeValueClamped(SprintFrictionSpeedRange, SprintFrictionOutput, Speed2D);
	}
	return GetGaitSettings(Gait).GroundFriction;
}

EBH_Gait UBH_StanceMovementProfile::GetBrakingBandForSpeed(float Speed2D) const
{
	if (Speed2D >= SprintStopSpeedThreshold)
	{
		return EBH_Gait::Sprint;
	}
	return Speed2D >= RunStopSpeedThreshold ? EBH_Gait::Run : EBH_Gait::Walk;
}

#if WITH_EDITOR
UBH_StanceMovementProfile* UBH_StanceMovementProfile::CreateProfileAsset(const FString& PackagePath, const FString& AssetName)
{
	UPackage* Package = CreatePackage(*(PackagePath / AssetName));
	Package->FullyLoad();
	UBH_StanceMovementProfile* Asset = NewObject<UBH_StanceMovementProfile>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
	FAssetRegistryModule::AssetCreated(Asset);
	Package->MarkPackageDirty();
	return Asset;
}
#endif
