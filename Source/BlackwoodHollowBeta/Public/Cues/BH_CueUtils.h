// Blackwood Hollow - shared helpers for the combat GameplayCue notifies

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraShakeBase.h"

class AActor;
class APlayerController;

DECLARE_LOG_CATEGORY_EXTERN(LogBHCue, Log, All);

namespace BH_CueUtils
{
	/**
	 * Freezes every USkeletalMeshComponent on Actor (GlobalAnimRateScale = 0) for Duration seconds of world time,
	 * then restores the previous value. Overlapping calls extend the freeze but the original rate is stored /
	 * restored exactly once. No global time dilation. Cosmetic: skipped on dedicated servers.
	 */
	void ApplyHitStop(AActor* Actor, float Duration);

	/** The local player controller whose pawn is one of the given actors (nullptr if none on this machine). */
	APlayerController* FindLocalControllerInvolving(const AActor* A, const AActor* B);

	/**
	 * Plays ShakeClass on the local player's camera if the local pawn is A or B.
	 * Direction (a world-space vector, e.g. the hit normal) biases the shake by rotating its play space
	 * (ECameraShakePlaySpace::UserDefined, yaw from Direction); a zero Direction uses CameraLocal.
	 * @return the started shake instance (nullptr if none).
	 */
	UCameraShakeBase* PlayDirectionalShake(const AActor* A, const AActor* B, TSubclassOf<UCameraShakeBase> ShakeClass, float Scale, const FVector& Direction);
}
