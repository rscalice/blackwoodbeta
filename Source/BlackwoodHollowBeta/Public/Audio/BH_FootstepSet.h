// Blackwood Hollow - Phase 10A footstep sound set (per-surface footsteps, armor layer, tunables)
// Target: Unreal Engine 5.8 (C++)
//
// One data asset (DA_FootstepSet, assigned in Project Settings > Game > Blackwood Hollow RPG > Audio > Default Footstep Set)
// holds every footstep sound in the game. UBH_AN_Footstep traces the ground under the foot, maps the hit physical material to
// an EPhysicalSurface (Project Settings > Physics > Physical Surfaces: Sand, Stone, Wood, Grass, Dirt, ShallowWater, Metal,
// Coral) and asks this asset for a sound.
//
// FALLBACKS (first non-empty list wins), for surface S and event E:
//   S[E] -> DefaultSurface[E] -> S[Fallback(E)] -> DefaultSurface[Fallback(E)] -> ... down the event fallback chain
//   Sprint -> Run -> Walk, Combat -> Run -> Walk, Crouch / Scuff / Jump -> Walk, Land -> Run -> Walk,
//   Handplant -> Scuff -> Walk, Roll -> Land -> Run -> Walk.
// A surface with no entry at all (or SurfaceType_Default) uses DefaultSurface.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Items/BH_EquipmentTypes.h"
#include "Audio/BH_FootstepTypes.h"
#include "BH_FootstepSet.generated.h"

class UNiagaraSystem;
class USoundAttenuation;
class USoundBase;

/** A list of alternative sounds for one event (a random one is picked, never the same twice in a row). */
USTRUCT(BlueprintType)
struct BLACKWOODHOLLOWBETA_API FBH_FootstepSoundList
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footsteps")
	TArray<TSoftObjectPtr<USoundBase>> Sounds;
};

/** Everything one surface sounds like: per-event sound lists plus an optional impact effect. */
USTRUCT(BlueprintType)
struct BLACKWOODHOLLOWBETA_API FBH_SurfaceFootsteps
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footsteps")
	TMap<EBH_FootstepEvent, FBH_FootstepSoundList> Events;

	/** Optional dust / splash / spark spawned at the hit point on every footstep on this surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footsteps")
	TSoftObjectPtr<UNiagaraSystem> ImpactFX;
};

/** Armor layer: extra rattle / cloth sounds played on top of the surface sound for one armor weight class. */
USTRUCT(BlueprintType)
struct BLACKWOODHOLLOWBETA_API FBH_ArmorFoley
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footsteps")
	TArray<TSoftObjectPtr<USoundBase>> Sounds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footsteps", meta = (ClampMin = "0.0"))
	float Volume = 0.5f;
};

UCLASS(BlueprintType, meta = (DisplayName = "Footstep Set"))
class BLACKWOODHOLLOWBETA_API UBH_FootstepSet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UBH_FootstepSet();

	// -- Sounds -------------------------------------------------------------------------------

	/** Per physical surface. Missing surface = DefaultSurface. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surfaces")
	TMap<TEnumAsByte<EPhysicalSurface>, FBH_SurfaceFootsteps> Surfaces;

	/** Used for SurfaceType_Default, for a surface with no entry, and for any event a surface has no sounds for. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Surfaces")
	FBH_SurfaceFootsteps DefaultSurface;

	/** Extra layer by the heaviest equipped armor piece (Cloth = no State.Armor.Weight.* tag). Missing class = nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Armor")
	TMap<EBH_ArmorWeightClass, FBH_ArmorFoley> ArmorFoley;

	// -- Tunables -----------------------------------------------------------------------------

	/** Base volume per event (before VolumeRange, notify multiplier and combat multiplier). Missing event = 1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tuning")
	TMap<EBH_FootstepEvent, float> EventBaseVolume;

	/** Random volume multiplier per step (x = min, y = max). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tuning")
	FVector2D VolumeRange = FVector2D(0.85, 1.0);

	/** Random pitch multiplier per step (x = min, y = max). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tuning")
	FVector2D PitchRange = FVector2D(0.95, 1.05);

	/** Footsteps fired from combat montages (EBH_FootstepEvent::Combat) are scaled by this (30-35% of normal). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tuning", meta = (ClampMin = "0.0"))
	float CombatVolumeMultiplier = 0.33f;

	/** The ground trace starts this far above the foot (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tuning", meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float TraceUp = 20.f;

	/** The ground trace ends this far below the foot (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tuning", meta = (ClampMin = "1.0", ForceUnits = "cm"))
	float TraceDown = 60.f;

	/** Minimum seconds between two footsteps from the same foot of the same mesh (blend overlap / double-notify guard). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tuning", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float MinRetriggerSeconds = 0.15f;

	/** Attenuation used for every footstep sound (null = the sound's own). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tuning")
	TObjectPtr<USoundAttenuation> Attenuation;

	// -- Helpers ------------------------------------------------------------------------------

	/** The event to try next when an event has no sounds (Sprint -> Run -> Walk, ...). Returns Event itself at the end of the chain. */
	static EBH_FootstepEvent GetFallbackEvent(EBH_FootstepEvent Event);

	/** The surface entry for Surface, or DefaultSurface when it has none. */
	const FBH_SurfaceFootsteps& GetSurfaceFootsteps(EPhysicalSurface Surface) const;

	/** First non-empty sound list for (Surface, Event) following the fallback rules in the file header. Null if nothing is configured at all. */
	const FBH_FootstepSoundList* ResolveSoundList(EPhysicalSurface Surface, EBH_FootstepEvent Event) const;

	/**
	 * Picks a random sound for (Surface, Event), never the same entry twice in a row for the same (Surface, Event).
	 * NOTE: uses TSoftObjectPtr::LoadSynchronous (footstep sounds are small; after the first step they are already loaded).
	 * Game thread only. Null if nothing is configured.
	 */
	USoundBase* PickSound(EPhysicalSurface Surface, EBH_FootstepEvent Event) const;

	/** Random armor sound for Weight (null if none configured); OutVolume = that class's volume. Same no-repeat rule. */
	USoundBase* PickArmorSound(EBH_ArmorWeightClass Weight, float& OutVolume) const;

	/** ImpactFX of the surface (falls back to DefaultSurface's). May be null; LoadSynchronous. */
	UNiagaraSystem* GetImpactFX(EPhysicalSurface Surface) const;

	float GetEventBaseVolume(EBH_FootstepEvent Event) const;

	/** Random volume / pitch multipliers from VolumeRange / PitchRange. */
	float RandomVolume() const;
	float RandomPitch() const;

private:
	USoundBase* PickFromSoft(const TArray<TSoftObjectPtr<USoundBase>>& List, int32 HistoryKey) const;

	/** Last picked index per history key (runtime only; game thread only). */
	mutable TMap<int32, int32> LastPick;
};
