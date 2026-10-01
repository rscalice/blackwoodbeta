// Blackwood Hollow - shared helpers for the combat GameplayCue notifies (implementation)

#include "Cues/BH_CueUtils.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY(LogBHCue);

namespace BH_CueUtils
{
	namespace
	{
		struct FHitStopEntry
		{
			float SavedScale = 1.f;
			uint32 Token = 0;
		};

		TMap<TWeakObjectPtr<USkeletalMeshComponent>, FHitStopEntry> GHitStops;
		uint32 GHitStopToken = 0;

		/** Last index picked per sound array (keyed on the array's storage), for the no-immediate-repeat rule. */
		TMap<const void*, int32> GLastSoundPick;

		bool IsCosmeticWorld(const UWorld* World)
		{
			return World && World->GetNetMode() != NM_DedicatedServer;
		}

		/** Picks a random index in [0, Num), different from the last pick for this key when Num >= 2. */
		int32 PickSoundIndex(const void* Key, int32 Num)
		{
			int32 Index = FMath::RandRange(0, Num - 1);
			int32& Last = GLastSoundPick.FindOrAdd(Key, INDEX_NONE);
			if (Num > 1 && Index == Last)
			{
				Index = (Index + FMath::RandRange(1, Num - 1)) % Num;
			}
			Last = Index;
			return Index;
		}
	}

	void ApplyHitStop(AActor* Actor, float Duration)
	{
		UWorld* World = Actor ? Actor->GetWorld() : nullptr;
		if (!World || Duration <= 0.f || World->GetNetMode() == NM_DedicatedServer)
		{
			return;
		}

		for (auto It = GHitStops.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid())
			{
				It.RemoveCurrent();
			}
		}

		TArray<USkeletalMeshComponent*> Meshes;
		Actor->GetComponents<USkeletalMeshComponent>(Meshes);
		if (Meshes.Num() == 0)
		{
			return;
		}

		const uint32 Token = ++GHitStopToken;
		TArray<TWeakObjectPtr<USkeletalMeshComponent>> Frozen;
		for (USkeletalMeshComponent* Mesh : Meshes)
		{
			if (!Mesh)
			{
				continue;
			}
			const TWeakObjectPtr<USkeletalMeshComponent> Key(Mesh);
			FHitStopEntry* Entry = GHitStops.Find(Key);
			if (!Entry)
			{
				Entry = &GHitStops.Add(Key);
				Entry->SavedScale = Mesh->GlobalAnimRateScale; // stored once per overlapping freeze
				Mesh->GlobalAnimRateScale = 0.f;
			}
			Entry->Token = Token; // latest freeze owns the restore
			Frozen.Add(Key);
		}

		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([Frozen, Token]()
		{
			for (const TWeakObjectPtr<USkeletalMeshComponent>& Key : Frozen)
			{
				FHitStopEntry* Entry = GHitStops.Find(Key);
				if (Entry && Entry->Token == Token)
				{
					if (USkeletalMeshComponent* Mesh = Key.Get())
					{
						Mesh->GlobalAnimRateScale = Entry->SavedScale;
					}
					GHitStops.Remove(Key);
				}
			}
		}), Duration, false);
	}

	APlayerController* FindLocalControllerInvolving(const AActor* A, const AActor* B)
	{
		const UWorld* World = A ? A->GetWorld() : (B ? B->GetWorld() : nullptr);
		if (!World)
		{
			return nullptr;
		}
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* PC = It->Get();
			if (PC && PC->IsLocalController())
			{
				const APawn* Pawn = PC->GetPawn();
				if (Pawn && (Pawn == A || Pawn == B))
				{
					return PC;
				}
			}
		}
		return nullptr;
	}

	UCameraShakeBase* PlayDirectionalShake(const AActor* A, const AActor* B, TSubclassOf<UCameraShakeBase> ShakeClass, float Scale, const FVector& Direction)
	{
		if (!ShakeClass || Scale <= 0.f)
		{
			return nullptr;
		}
		const APlayerController* PC = FindLocalControllerInvolving(A, B);
		if (!PC || !PC->PlayerCameraManager)
		{
			return nullptr;
		}

		if (Direction.SizeSquared2D() > KINDA_SMALL_NUMBER)
		{
			const FRotator PlaySpaceRot(0.f, Direction.Rotation().Yaw, 0.f);
			return PC->PlayerCameraManager->StartCameraShake(ShakeClass, Scale, ECameraShakePlaySpace::UserDefined, PlaySpaceRot);
		}
		return PC->PlayerCameraManager->StartCameraShake(ShakeClass, Scale, ECameraShakePlaySpace::CameraLocal);
	}

	USoundBase* PlayRandomSound(const UObject* WorldContext, const TArray<TObjectPtr<USoundBase>>& Sounds, FVector Location,
		FVector2D VolumeRange, FVector2D PitchRange, USoundAttenuation* Attenuation)
	{
		UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
		if (!IsCosmeticWorld(World) || Sounds.Num() == 0)
		{
			return nullptr;
		}

		// Skip empty slots without breaking the repeat rule (pick among the array as authored).
		USoundBase* Sound = Sounds[PickSoundIndex(Sounds.GetData(), Sounds.Num())];
		if (!Sound)
		{
			return nullptr;
		}

		const float Volume = FMath::FRandRange(FMath::Min(VolumeRange.X, VolumeRange.Y), FMath::Max(VolumeRange.X, VolumeRange.Y));
		const float Pitch = FMath::FRandRange(FMath::Min(PitchRange.X, PitchRange.Y), FMath::Max(PitchRange.X, PitchRange.Y));
		UE_LOG(LogBHCue, Verbose, TEXT("PlayRandomSound: %s vol=%.2f pitch=%.2f at %s"), *GetNameSafe(Sound), Volume, Pitch, *Location.ToCompactString());
		UGameplayStatics::PlaySoundAtLocation(World, Sound, Location, FRotator::ZeroRotator, Volume, Pitch, 0.f, Attenuation);
		return Sound;
	}

	UNiagaraComponent* SpawnOneShotNiagara(const UObject* WorldContext, UNiagaraSystem* System, FVector Location, FRotator Rotation,
		FVector Scale, float MaxLifetime)
	{
		UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
		if (!System || !IsCosmeticWorld(World))
		{
			return nullptr;
		}

		UNiagaraComponent* Comp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, System, Location, Rotation, Scale,
			/*bAutoDestroy*/ true, /*bAutoActivate*/ true);
		if (Comp && MaxLifetime > 0.f)
		{
			// Looping systems never finish on their own: Deactivate() lets live particles fade, then auto-destroy cleans up.
			const TWeakObjectPtr<UNiagaraComponent> WeakComp(Comp);
			FTimerHandle Handle;
			World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakComp]()
			{
				if (UNiagaraComponent* Live = WeakComp.Get())
				{
					Live->Deactivate();
				}
			}), MaxLifetime, false);
		}
		return Comp;
	}

	UNiagaraComponent* PlayCueFX(const UObject* WorldContext, const FBH_CueFX& FX, const FVector& Location, const FRotator& Rotation,
		USoundAttenuation* Attenuation)
	{
		const FVector SpawnLocation = Location + FX.Offset;
		UE_LOG(LogBHCue, Verbose, TEXT("PlayCueFX: system=%s sounds=%d scale=%s life=%.2f at %s"),
			*GetNameSafe(FX.System), FX.Sounds.Num(), *FX.Scale.ToCompactString(), FX.MaxLifetime, *SpawnLocation.ToCompactString());

		PlayRandomSound(WorldContext, FX.Sounds, SpawnLocation, FX.VolumeRange, FX.PitchRange, Attenuation);
		return SpawnOneShotNiagara(WorldContext, FX.System, SpawnLocation, Rotation, FX.Scale, FX.MaxLifetime);
	}
}
