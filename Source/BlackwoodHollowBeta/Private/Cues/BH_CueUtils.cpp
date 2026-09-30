// Blackwood Hollow - shared helpers for the combat GameplayCue notifies (implementation)

#include "Cues/BH_CueUtils.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
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
}
