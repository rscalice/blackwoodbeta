// Blackwood Hollow - crab anim instance (Phase 8C)
// Target: Unreal Engine 5.8 (C++)
//
// Native data source for ABP_BH_Crab. The game-thread pass (NativeUpdateAnimation) copies what only the game thread may read
// (velocity, actor rotation, the replicated action phase, the Blighted tag); the thread-safe pass
// (NativeThreadSafeUpdateAnimation) derives the values the AnimGraph reads:
//
//   Speed                 horizontal speed, cm/s
//   bIsMoving             Speed > MoveSpeedThreshold (10 cm/s)
//   MoveDirectionAngle    -180..180 degrees: velocity direction relative to the body's facing (0 = forward, +90 = to the right).
//                         The crab faces its target and scuttles sideways, so the ABP leans / blends by this angle.
//   PlayRateForScuttle    Speed / ReferenceScuttleSpeed, clamped: the play rate of the scuttle loop (Crabix_Anim)
//   ActionPhase           EBH_CrabActionPhase from ABH_EnemyCrab (Idle, JabWindup, JabStrike, PinchWindup, PinchSnap,
//                         Sidestep, HitReact, Stagger, Dead)
//   bIsBlighted           State.Status.Blighted is on the crab
//
// Wire these in the AnimGraph with Property Access or by reading the variables in a state machine transition.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Characters/BH_CrabTypes.h"
#include "BH_CrabAnimInstance.generated.h"

class ABH_EnemyCrab;

UCLASS(Blueprintable, BlueprintType)
class BLACKWOODHOLLOWBETA_API UBH_CrabAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	// -- Tuning --------------------------------------------------------------------------------

	/** Ground speed (cm/s) at which the scuttle loop plays at its authored rate (PlayRateForScuttle = 1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crab|Tuning", meta = (ClampMin = "1"))
	float ReferenceScuttleSpeed = 145.f;

	/** Speed (cm/s) above which bIsMoving is true. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crab|Tuning", meta = (ClampMin = "0"))
	float MoveSpeedThreshold = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crab|Tuning", meta = (ClampMin = "0"))
	float MinPlayRate = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crab|Tuning", meta = (ClampMin = "0"))
	float MaxPlayRate = 2.5f;

	// -- Outputs (read by the AnimGraph) -------------------------------------------------------

	UPROPERTY(BlueprintReadOnly, Category = "Crab|State")
	float Speed = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Crab|State")
	bool bIsMoving = false;

	UPROPERTY(BlueprintReadOnly, Category = "Crab|State")
	float MoveDirectionAngle = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Crab|State")
	float PlayRateForScuttle = 1.f;

	UPROPERTY(BlueprintReadOnly, Category = "Crab|State")
	EBH_CrabActionPhase ActionPhase = EBH_CrabActionPhase::Idle;

	UPROPERTY(BlueprintReadOnly, Category = "Crab|State")
	bool bIsBlighted = false;

	UPROPERTY(BlueprintReadOnly, Category = "Crab|State")
	bool bIsDead = false;

	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual void NativeThreadSafeUpdateAnimation(float DeltaSeconds) override;

private:
	UPROPERTY(Transient)
	TWeakObjectPtr<ABH_EnemyCrab> Crab;

	// Game-thread snapshot consumed by NativeThreadSafeUpdateAnimation.
	FVector SampledVelocity = FVector::ZeroVector;
	float SampledActorYaw = 0.f;
};
