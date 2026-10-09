// Blackwood Hollow - Phase 10A AnimNotify that plays a surface-aware footstep (implementation)

#include "Animation/BH_AN_Footstep.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimNotifyLibrary.h"
#include "Audio/BH_FootstepSet.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Items/BH_EquipmentTypes.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Progression/BH_RPGSettings.h"
#include "Sound/SoundBase.h"

namespace BH_Footstep_Private
{
	using FRateKey = TTuple<TWeakObjectPtr<const USkeletalMeshComponent>, uint8>;

	/** World time of the last footstep per (mesh, foot). Game thread only. */
	static TMap<FRateKey, double> LastStepTime;

	/** @return true (and records the step) if this foot of this mesh may play now. */
	static bool PassRateLimit(const USkeletalMeshComponent* Mesh, EBH_FootstepFoot Foot, double Now, float MinInterval)
	{
		// Prune stale entries (dead meshes, or older than 2s) once the map grows.
		if (LastStepTime.Num() > 64)
		{
			for (auto It = LastStepTime.CreateIterator(); It; ++It)
			{
				if (!It.Key().Key.IsValid() || Now - It.Value() > 2.0 || It.Value() > Now)
				{
					It.RemoveCurrent();
				}
			}
		}

		const FRateKey Key(Mesh, static_cast<uint8>(Foot));
		if (const double* Last = LastStepTime.Find(Key))
		{
			// A newer time than Now means the clock restarted (new world / PIE): treat as stale.
			if (Now >= *Last && Now - *Last < MinInterval)
			{
				return false;
			}
		}
		LastStepTime.Add(Key, Now);
		return true;
	}

	static EBH_ArmorWeightClass GetOwnerArmorWeight(AActor* Owner)
	{
		UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Owner);
		if (ASC)
		{
			if (ASC->HasMatchingGameplayTag(TAG_State_Armor_Weight_Heavy))
			{
				return EBH_ArmorWeightClass::Heavy;
			}
			if (ASC->HasMatchingGameplayTag(TAG_State_Armor_Weight_Medium))
			{
				return EBH_ArmorWeightClass::Medium;
			}
		}
		return EBH_ArmorWeightClass::Light; // no tag = Light
	}
}

UBH_AN_Footstep::UBH_AN_Footstep()
{
#if WITH_EDITORONLY_DATA
	NotifyColor = FColor(120, 220, 140);
#endif
}

FString UBH_AN_Footstep::GetNotifyName_Implementation() const
{
	const UEnum* FootEnum = StaticEnum<EBH_FootstepFoot>();
	const UEnum* EventEnum = StaticEnum<EBH_FootstepEvent>();
	const FString FootName = FootEnum ? FootEnum->GetNameStringByValue(static_cast<int64>(Foot)) : TEXT("?");
	const FString EventName = EventEnum ? EventEnum->GetNameStringByValue(static_cast<int64>(Event)) : TEXT("?");

	const TCHAR* FootLabel = Foot == EBH_FootstepFoot::Left ? TEXT("L") : (Foot == EBH_FootstepFoot::Right ? TEXT("R") : *FootName);
	return FString::Printf(TEXT("Footstep %s %s"), FootLabel, *EventName);
}

void UBH_AN_Footstep::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);
	using namespace BH_Footstep_Private;

	// Anim previews / editor tools can call us without a world or an owner: play nothing.
	if (!MeshComp)
	{
		return;
	}
	UWorld* World = MeshComp->GetWorld();
	AActor* Owner = MeshComp->GetOwner();
	if (!World || !Owner)
	{
		return;
	}

	// Cosmetic: no audio on dedicated servers.
	if (World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	// Same check GASP's foley notifies make: no steps from a montage / source that is blending out.
	if (UAnimNotifyLibrary::IsBlendingOut(EventReference))
	{
		return;
	}

	UBH_FootstepSet* Set = FootstepSet;
	if (!Set)
	{
		Set = UBH_RPGSettings::Get()->GetDefaultFootstepSet();
	}
	if (!Set)
	{
		return;
	}

	if (!PassRateLimit(MeshComp, Foot, World->GetTimeSeconds(), Set->MinRetriggerSeconds))
	{
		return;
	}

	// Trace origin: the foot bone, or the mesh origin for Root events / a missing bone.
	FVector Base = MeshComp->GetComponentLocation();
	if (Foot != EBH_FootstepFoot::Root)
	{
		const FName BoneName = (Foot == EBH_FootstepFoot::Left) ? LeftFootBone : RightFootBone;
		if (!BoneName.IsNone() && MeshComp->GetBoneIndex(BoneName) != INDEX_NONE)
		{
			Base = MeshComp->GetBoneLocation(BoneName, EBoneSpaces::WorldSpace);
		}
	}

	const FVector Start = Base + FVector::UpVector * Set->TraceUp;
	const FVector End = Base - FVector::UpVector * Set->TraceDown;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(BHFootstep), /*bTraceComplex*/ false, Owner);
	Params.bReturnPhysicalMaterial = true;

	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);

	EPhysicalSurface Surface = SurfaceType_Default;
	FVector Location = Base;
	FVector Normal = FVector::UpVector;
	if (bHit)
	{
		Surface = UPhysicalMaterial::DetermineSurfaceType(Hit.PhysMaterial.Get());
		Location = Hit.ImpactPoint;
		Normal = Hit.ImpactNormal;
	}

	float Volume = Set->GetEventBaseVolume(Event) * VolumeMultiplier * Set->RandomVolume();
	if (Event == EBH_FootstepEvent::Combat)
	{
		Volume *= Set->CombatVolumeMultiplier;
	}
	const float Pitch = Set->RandomPitch();

	if (USoundBase* Sound = Set->PickSound(Surface, Event))
	{
		UGameplayStatics::SpawnSoundAtLocation(World, Sound, Location, FRotator::ZeroRotator, Volume, Pitch, 0.f, Set->Attenuation);
	}

	// Armor layer (Light when the owner has no State.Armor.Weight.* tag).
	const EBH_ArmorWeightClass Weight = GetOwnerArmorWeight(Owner);
	float ArmorVolume = 0.f;
	if (USoundBase* ArmorSound = Set->PickArmorSound(Weight, ArmorVolume))
	{
		float FinalArmorVolume = ArmorVolume * VolumeMultiplier * Set->RandomVolume();
		if (Event == EBH_FootstepEvent::Combat)
		{
			FinalArmorVolume *= Set->CombatVolumeMultiplier;
		}
		UGameplayStatics::SpawnSoundAtLocation(World, ArmorSound, Location, FRotator::ZeroRotator, FinalArmorVolume, Set->RandomPitch(), 0.f, Set->Attenuation);
	}

	if (bHit)
	{
		if (UNiagaraSystem* FX = Set->GetImpactFX(Surface))
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, FX, Location, Normal.Rotation(), FVector::OneVector, /*bAutoDestroy*/ true);
		}
	}
}
