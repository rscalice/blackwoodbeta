// Blackwood Hollow - Phase 10A footstep sound set (implementation)

#include "Audio/BH_FootstepSet.h"
#include "NiagaraSystem.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"

UBH_FootstepSet::UBH_FootstepSet()
{
	// Sensible starting volumes; every one is editable on the asset.
	EventBaseVolume.Add(EBH_FootstepEvent::Walk, 0.5f);
	EventBaseVolume.Add(EBH_FootstepEvent::Run, 0.7f);
	EventBaseVolume.Add(EBH_FootstepEvent::Sprint, 0.85f);
	EventBaseVolume.Add(EBH_FootstepEvent::Crouch, 0.3f);
	EventBaseVolume.Add(EBH_FootstepEvent::Land, 1.0f);
	EventBaseVolume.Add(EBH_FootstepEvent::Scuff, 0.4f);
	EventBaseVolume.Add(EBH_FootstepEvent::Jump, 0.6f);
	EventBaseVolume.Add(EBH_FootstepEvent::Handplant, 0.6f);
	EventBaseVolume.Add(EBH_FootstepEvent::Roll, 0.8f);
	EventBaseVolume.Add(EBH_FootstepEvent::Combat, 1.0f);
}

EBH_FootstepEvent UBH_FootstepSet::GetFallbackEvent(EBH_FootstepEvent Event)
{
	switch (Event)
	{
	case EBH_FootstepEvent::Sprint:    return EBH_FootstepEvent::Run;
	case EBH_FootstepEvent::Combat:    return EBH_FootstepEvent::Run;
	case EBH_FootstepEvent::Run:       return EBH_FootstepEvent::Walk;
	case EBH_FootstepEvent::Crouch:    return EBH_FootstepEvent::Walk;
	case EBH_FootstepEvent::Scuff:     return EBH_FootstepEvent::Walk;
	case EBH_FootstepEvent::Jump:      return EBH_FootstepEvent::Walk;
	case EBH_FootstepEvent::Land:      return EBH_FootstepEvent::Run;
	case EBH_FootstepEvent::Handplant: return EBH_FootstepEvent::Scuff;
	case EBH_FootstepEvent::Roll:      return EBH_FootstepEvent::Land;
	default:                           return Event; // Walk: end of the chain
	}
}

const FBH_SurfaceFootsteps& UBH_FootstepSet::GetSurfaceFootsteps(EPhysicalSurface Surface) const
{
	if (Surface != SurfaceType_Default)
	{
		if (const FBH_SurfaceFootsteps* Found = Surfaces.Find(TEnumAsByte<EPhysicalSurface>(Surface)))
		{
			return *Found;
		}
	}
	return DefaultSurface;
}

const FBH_FootstepSoundList* UBH_FootstepSet::ResolveSoundList(EPhysicalSurface Surface, EBH_FootstepEvent Event) const
{
	const FBH_SurfaceFootsteps& SurfaceData = GetSurfaceFootsteps(Surface);

	EBH_FootstepEvent Current = Event;
	for (int32 Guard = 0; Guard < 8; ++Guard)
	{
		if (const FBH_FootstepSoundList* List = SurfaceData.Events.Find(Current))
		{
			if (List->Sounds.Num() > 0)
			{
				return List;
			}
		}
		if (&SurfaceData != &DefaultSurface)
		{
			if (const FBH_FootstepSoundList* List = DefaultSurface.Events.Find(Current))
			{
				if (List->Sounds.Num() > 0)
				{
					return List;
				}
			}
		}

		const EBH_FootstepEvent Next = GetFallbackEvent(Current);
		if (Next == Current)
		{
			break;
		}
		Current = Next;
	}
	return nullptr;
}

USoundBase* UBH_FootstepSet::PickFromSoft(const TArray<TSoftObjectPtr<USoundBase>>& List, int32 HistoryKey) const
{
	TArray<int32, TInlineAllocator<16>> Valid;
	for (int32 i = 0; i < List.Num(); ++i)
	{
		if (!List[i].IsNull())
		{
			Valid.Add(i);
		}
	}
	if (Valid.IsEmpty())
	{
		return nullptr;
	}

	const int32* Last = LastPick.Find(HistoryKey);
	int32 Chosen = Valid[FMath::RandRange(0, Valid.Num() - 1)];
	if (Valid.Num() > 1 && Last && Chosen == *Last)
	{
		// Re-roll among the others: pick a random offset 1..N-1 from the repeated entry.
		const int32 Pos = Valid.IndexOfByKey(Chosen);
		Chosen = Valid[(Pos + FMath::RandRange(1, Valid.Num() - 1)) % Valid.Num()];
	}
	LastPick.Add(HistoryKey, Chosen);

	return List[Chosen].LoadSynchronous(); // small one-shots; already resident after first use
}

USoundBase* UBH_FootstepSet::PickSound(EPhysicalSurface Surface, EBH_FootstepEvent Event) const
{
	const FBH_FootstepSoundList* List = ResolveSoundList(Surface, Event);
	if (!List)
	{
		return nullptr;
	}
	const int32 Key = (static_cast<int32>(Surface) << 8) | static_cast<int32>(Event);
	return PickFromSoft(List->Sounds, Key);
}

USoundBase* UBH_FootstepSet::PickArmorSound(EBH_ArmorWeightClass Weight, float& OutVolume) const
{
	const FBH_ArmorFoley* Foley = ArmorFoley.Find(Weight);
	if (!Foley || Foley->Sounds.IsEmpty())
	{
		return nullptr;
	}
	OutVolume = Foley->Volume;
	// Negative keys never collide with the (surface << 8 | event) keys above.
	return PickFromSoft(Foley->Sounds, -1 - static_cast<int32>(Weight));
}

UNiagaraSystem* UBH_FootstepSet::GetImpactFX(EPhysicalSurface Surface) const
{
	const FBH_SurfaceFootsteps& SurfaceData = GetSurfaceFootsteps(Surface);
	if (!SurfaceData.ImpactFX.IsNull())
	{
		return SurfaceData.ImpactFX.LoadSynchronous();
	}
	if (&SurfaceData != &DefaultSurface && !DefaultSurface.ImpactFX.IsNull())
	{
		return DefaultSurface.ImpactFX.LoadSynchronous();
	}
	return nullptr;
}

float UBH_FootstepSet::GetEventBaseVolume(EBH_FootstepEvent Event) const
{
	const float* Volume = EventBaseVolume.Find(Event);
	return Volume ? *Volume : 1.f;
}

float UBH_FootstepSet::RandomVolume() const
{
	return FMath::FRandRange(FMath::Min(VolumeRange.X, VolumeRange.Y), FMath::Max(VolumeRange.X, VolumeRange.Y));
}

float UBH_FootstepSet::RandomPitch() const
{
	return FMath::FRandRange(FMath::Min(PitchRange.X, PitchRange.Y), FMath::Max(PitchRange.X, PitchRange.Y));
}
