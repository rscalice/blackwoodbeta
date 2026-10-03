// Blackwood Hollow - per-stance movement tuning (speeds, acceleration, braking, friction) for ABH_CharacterBase

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Characters/BH_CharacterTypes.h"
#include "BH_StanceMovementProfile.generated.h"

/** Movement values for one gait. */
USTRUCT(BlueprintType)
struct FBH_GaitMovementSettings
{
	GENERATED_BODY()

	/** X forward, Y strafe, Z backward (GASP convention, mapped by StrafeSpeedMap 0..2). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FVector Speeds = FVector(500.0, 350.0, 300.0);

	/** Used when this gait is active and has no speed ramp (Sprint uses the ramps on the profile). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = 0))
	float MaxAcceleration = 800.f;

	/** BrakingDecelerationWalking with no movement input. Picked by the latched speed band (ABH_CharacterBase). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = 0))
	float BrakingDecelerationNoInput = 2000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = 0))
	float GroundFriction = 5.f;
};

/** One asset per weapon stance, referenced from UBH_StanceComponent::MovementProfiles. */
UCLASS(BlueprintType)
class BLACKWOODHOLLOWBETA_API UBH_StanceMovementProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gaits")
	FBH_GaitMovementSettings Walk;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gaits")
	FBH_GaitMovementSettings Run;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gaits")
	FBH_GaitMovementSettings Sprint;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gaits")
	FVector CrouchSpeeds = FVector(225.0, 200.0, 180.0);

	/** BrakingDecelerationWalking while movement input is held (GASP: 500). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Braking")
	float BrakingDecelerationWithInput = 500.f;

	/** Sprint acceleration ramp: MapRangeClamped(Speed2D, SpeedRange.X..Y, Output.X..Y). GASP: (300..700) -> (800..300). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sprint ramps")
	FVector2D SprintAccelSpeedRange = FVector2D(300.0, 700.0);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sprint ramps")
	FVector2D SprintAccelOutput = FVector2D(800.0, 300.0);

	/** Sprint friction ramp. GASP: (0..500) -> (5..3). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sprint ramps")
	FVector2D SprintFrictionSpeedRange = FVector2D(0.0, 500.0);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sprint ramps")
	FVector2D SprintFrictionOutput = FVector2D(5.0, 3.0);

	/** Mirror of the stance chooser's Stand Idles Speed2D rows (walk-stop min / run-stop min / sprint-stop min). Also the braking latch bands. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chooser")
	float WalkStopSpeedThreshold = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chooser")
	float RunStopSpeedThreshold = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chooser")
	float SprintStopSpeedThreshold = 550.f;

	/** Informational: the Motion Matching node's PlayRate clamp. Loop speed x this range must contain the gait speed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Motion Matching")
	FFloatInterval MotionMatchingPlayRateRange = FFloatInterval(0.85f, 1.15f);

	/** Walk, Run or Sprint settings. */
	UFUNCTION(BlueprintPure, Category = "BH|Movement")
	const FBH_GaitMovementSettings& GetGaitSettings(EBH_Gait Gait) const;

	/** Sprint follows the acceleration ramp, other gaits their MaxAcceleration. */
	UFUNCTION(BlueprintPure, Category = "BH|Movement")
	float GetMaxAccelerationFor(EBH_Gait Gait, float Speed2D) const;

	/** Sprint follows the friction ramp, other gaits their GroundFriction. */
	UFUNCTION(BlueprintPure, Category = "BH|Movement")
	float GetGroundFrictionFor(EBH_Gait Gait, float Speed2D) const;

	/** Speed band a stop starts in: at or above SprintStop -> Sprint, at or above RunStop -> Run, else Walk. */
	UFUNCTION(BlueprintPure, Category = "BH|Movement")
	EBH_Gait GetBrakingBandForSpeed(float Speed2D) const;

#if WITH_EDITOR
	/** Editor-only asset creation via a plain package + NewObject. PackagePath e.g. "/Game/BlackwoodHollow/Animation/Stances/Data". Returns the new, unsaved object. */
	UFUNCTION(BlueprintCallable, Category = "BH|Editor", meta = (DevelopmentOnly))
	static UBH_StanceMovementProfile* CreateProfileAsset(const FString& PackagePath, const FString& AssetName);
#endif

	virtual FPrimaryAssetId GetPrimaryAssetId() const override { return FPrimaryAssetId("StanceMovementProfile", GetFName()); }
};
