// Blackwood Hollow - shared helpers for the combat GameplayCue notifies

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraShakeBase.h"
#include "Cues/BH_CueFX.h"

class AActor;
class APlayerController;
class UNiagaraComponent;
class UNiagaraSystem;
class USoundAttenuation;
class USoundBase;

DECLARE_LOG_CATEGORY_EXTERN(LogBHCue, Log, All);

namespace BH_CueUtils
{
	/**
	 * Freezes every USkeletalMeshComponent on Actor (GlobalAnimRateScale = FreezeScale, near zero) for Duration seconds of
	 * world time, then restores the previous value. Overlap safe: the original rate is stored once per mesh, every call
	 * pushes the mesh's end time out (never in), and only the timer that reaches the LATEST end time restores. No actor or
	 * global time dilation. Cosmetic: skipped on dedicated servers.
	 */
	void ApplyHitStop(AActor* Actor, float Duration, float FreezeScale = 0.f);

	/** The local player controller whose pawn is one of the given actors (nullptr if none on this machine). */
	APlayerController* FindLocalControllerInvolving(const AActor* A, const AActor* B);

	/**
	 * Plays ShakeClass on the local player's camera if the local pawn is A or B.
	 * Direction (a world-space vector, e.g. the hit normal) biases the shake by rotating its play space
	 * (ECameraShakePlaySpace::UserDefined, yaw from Direction); a zero Direction uses CameraLocal.
	 * @return the started shake instance (nullptr if none).
	 */
	UCameraShakeBase* PlayDirectionalShake(const AActor* A, const AActor* B, TSubclassOf<UCameraShakeBase> ShakeClass, float Scale, const FVector& Direction);

	/**
	 * Plays a random entry of Sounds at Location with a random volume / pitch from the given ranges.
	 * Avoids repeating the previous pick for the same array when it has 2+ entries (memory is keyed on the array's
	 * storage, so every FX block / notify keeps its own history). Cosmetic: skipped on dedicated servers.
	 * @return the sound that was picked (nullptr if none played).
	 */
	USoundBase* PlayRandomSound(const UObject* WorldContext, const TArray<TObjectPtr<USoundBase>>& Sounds, FVector Location,
		FVector2D VolumeRange, FVector2D PitchRange, USoundAttenuation* Attenuation);

	/**
	 * Spawns a fire-and-forget Niagara system (auto-destroys when finished). If MaxLifetime > 0 the component is
	 * deactivated after that many seconds, so looping systems cannot leak. Skipped on dedicated servers.
	 * @return the spawned component (nullptr if nothing spawned).
	 */
	UNiagaraComponent* SpawnOneShotNiagara(const UObject* WorldContext, UNiagaraSystem* System, FVector Location, FRotator Rotation,
		FVector Scale, float MaxLifetime);

	/** Plays one FBH_CueFX block (system + random sound) at Location + FX.Offset. @return the spawned component, if any. */
	UNiagaraComponent* PlayCueFX(const UObject* WorldContext, const FBH_CueFX& FX, const FVector& Location, const FRotator& Rotation,
		USoundAttenuation* Attenuation);
}
