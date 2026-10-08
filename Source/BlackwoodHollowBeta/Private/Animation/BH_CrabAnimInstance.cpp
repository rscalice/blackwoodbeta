// Blackwood Hollow - crab anim instance (implementation)

#include "Animation/BH_CrabAnimInstance.h"
#include "Characters/BH_EnemyCrab.h"

void UBH_CrabAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	Crab = Cast<ABH_EnemyCrab>(GetOwningActor());
}

void UBH_CrabAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (!Crab.IsValid())
	{
		Crab = Cast<ABH_EnemyCrab>(GetOwningActor());
	}
	const ABH_EnemyCrab* CrabPtr = Crab.Get();
	if (!CrabPtr)
	{
		SampledVelocity = FVector::ZeroVector;
		return;
	}

	SampledVelocity = CrabPtr->GetVelocity();
	SampledActorYaw = CrabPtr->GetActorRotation().Yaw;
	ActionPhase = CrabPtr->GetActionPhase();
	bIsDead = (ActionPhase == EBH_CrabActionPhase::Dead);
}

void UBH_CrabAnimInstance::NativeThreadSafeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeThreadSafeUpdateAnimation(DeltaSeconds);

	const FVector Flat(SampledVelocity.X, SampledVelocity.Y, 0.0);
	Speed = static_cast<float>(Flat.Size());
	bIsMoving = Speed > MoveSpeedThreshold;

	if (bIsMoving)
	{
		const float VelocityYaw = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Flat.Y, Flat.X)));
		MoveDirectionAngle = FMath::FindDeltaAngleDegrees(SampledActorYaw, VelocityYaw);
	}
	else
	{
		MoveDirectionAngle = 0.f;
	}

	PlayRateForScuttle = bIsMoving
		? FMath::Clamp(Speed / FMath::Max(ReferenceScuttleSpeed, 1.f), MinPlayRate, MaxPlayRate)
		: 1.f;
}
