// Blackwood Hollow - Melee hitbox AnimNotifyState (implementation)

#include "Animation/ANS_MeleeHitbox.h"
#include "AbilitySystem/BH_CombatFunctionLibrary.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequenceBase.h"
#include "Components/MeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "DrawDebugHelpers.h"

UANS_MeleeHitbox::UANS_MeleeHitbox()
{
	HitObjectChannels.Add(ECC_Pawn);
	HitObjectChannels.Add(ECC_PhysicsBody);

#if WITH_EDITORONLY_DATA
	NotifyColor = FColor(255, 70, 50);
#endif
}

FString UANS_MeleeHitbox::GetNotifyName_Implementation() const
{
	return FString::Printf(TEXT("Melee Hitbox (%s x%.2f)"),
		WeaponSlot == EBH_WeaponSlot::OffHand ? TEXT("Off") : TEXT("Main"), DamageMultiplier);
}

bool UANS_MeleeHitbox::GetWeaponPoints(USkeletalMeshComponent* MeshComp, TArray<FVector>& OutPoints) const
{
	OutPoints.Reset();
	if (!MeshComp)
	{
		return false;
	}

	FVector Root = FVector::ZeroVector;
	FVector Tip = FVector::ZeroVector;
	bool bFound = false;

	// 1) Sockets on the equipped weapon mesh.
	UMeshComponent* Weapon = UBH_CombatFunctionLibrary::GetEquippedWeaponComponent(MeshComp->GetOwner(), WeaponSlot);
	if (Weapon && Weapon->DoesSocketExist(RootSocketName) && Weapon->DoesSocketExist(TipSocketName))
	{
		Root = Weapon->GetSocketLocation(RootSocketName);
		Tip = Weapon->GetSocketLocation(TipSocketName);
		bFound = true;
	}
	// 2) Longest axis of the weapon's local bounds.
	else if (Weapon && bUseWeaponBoundsIfSocketsMissing)
	{
		const FBox LocalBox = Weapon->CalcBounds(FTransform::Identity).GetBox();
		if (LocalBox.IsValid)
		{
			const FVector Center = LocalBox.GetCenter();
			const FVector Extent = LocalBox.GetExtent();
			int32 Axis = 0;
			if (Extent.Y > Extent[Axis]) { Axis = 1; }
			if (Extent.Z > Extent[Axis]) { Axis = 2; }

			FVector AxisOffset = FVector::ZeroVector;
			AxisOffset[Axis] = Extent[Axis];

			const FTransform& WeaponTransform = Weapon->GetComponentTransform();
			Root = WeaponTransform.TransformPosition(Center - AxisOffset);
			Tip = WeaponTransform.TransformPosition(Center + AxisOffset);
			bFound = true;
		}
	}
	// 3) Same socket names on the animating character mesh (unarmed / debug setups).
	if (!bFound && MeshComp->DoesSocketExist(RootSocketName) && MeshComp->DoesSocketExist(TipSocketName))
	{
		Root = MeshComp->GetSocketLocation(RootSocketName);
		Tip = MeshComp->GetSocketLocation(TipSocketName);
		bFound = true;
	}

	if (!bFound)
	{
		return false;
	}

	const int32 NumSamples = FMath::Max(2, TraceSamples);
	OutPoints.Reserve(NumSamples);
	for (int32 Index = 0; Index < NumSamples; ++Index)
	{
		const float Alpha = static_cast<float>(Index) / static_cast<float>(NumSamples - 1);
		OutPoints.Add(FMath::Lerp(Root, Tip, Alpha));
	}
	return true;
}

void UANS_MeleeHitbox::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr;
	if (!Owner || Owner->GetLocalRole() == ROLE_SimulatedProxy)
	{
		// Simulated proxies never own the damage decision; skip the traces entirely.
		return;
	}

	FSwingState& Swing = ActiveSwings.FindOrAdd(MeshComp);
	Swing.HitActors.Reset();
	GetWeaponPoints(MeshComp, Swing.PreviousPoints);

	// Catch anything already overlapping the blade on the first frame.
	if (Swing.PreviousPoints.Num() >= 2)
	{
		SweepSegment(MeshComp, Animation, Swing, Swing.PreviousPoints[0], Swing.PreviousPoints.Last());
	}
}

void UANS_MeleeHitbox::NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float FrameDeltaTime,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyTick(MeshComp, Animation, FrameDeltaTime, EventReference);

	FSwingState* Swing = ActiveSwings.Find(MeshComp);
	if (!Swing)
	{
		return;
	}

	TArray<FVector> CurrentPoints;
	if (!GetWeaponPoints(MeshComp, CurrentPoints))
	{
		return;
	}

	if (Swing->PreviousPoints.Num() == CurrentPoints.Num())
	{
		for (int32 Index = 0; Index < CurrentPoints.Num(); ++Index)
		{
			SweepSegment(MeshComp, Animation, *Swing, Swing->PreviousPoints[Index], CurrentPoints[Index]);
		}
	}
	else
	{
		// Weapon appeared mid-window (or sample count changed): sweep the blade as it is now.
		SweepSegment(MeshComp, Animation, *Swing, CurrentPoints[0], CurrentPoints.Last());
	}

	Swing->PreviousPoints = MoveTemp(CurrentPoints);
}

void UANS_MeleeHitbox::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	ActiveSwings.Remove(MeshComp);

	// Drop entries for meshes that were destroyed mid-swing.
	for (auto It = ActiveSwings.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}

	Super::NotifyEnd(MeshComp, Animation, EventReference);
}

void UANS_MeleeHitbox::SweepSegment(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, FSwingState& Swing,
	const FVector& Start, const FVector& End) const
{
	AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr;
	UWorld* World = MeshComp ? MeshComp->GetWorld() : nullptr;
	if (!Owner || !World)
	{
		return;
	}

	FCollisionObjectQueryParams ObjectParams;
	for (const TEnumAsByte<ECollisionChannel>& Channel : HitObjectChannels)
	{
		ObjectParams.AddObjectTypesToQuery(Channel.GetValue());
	}
	if (!ObjectParams.IsValid())
	{
		return;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(BH_MeleeHitbox), /*bTraceComplex*/ false, Owner);
	TArray<AActor*> AttachedActors;
	Owner->GetAttachedActors(AttachedActors);
	QueryParams.AddIgnoredActors(AttachedActors);

	TArray<FHitResult> Hits;
	World->SweepMultiByObjectType(Hits, Start, End, FQuat::Identity, ObjectParams, FCollisionShape::MakeSphere(TraceRadius), QueryParams);

#if ENABLE_DRAW_DEBUG
	if (bDrawDebug)
	{
		const FColor Color = Hits.Num() > 0 ? FColor::Red : FColor::Green;
		DrawDebugLine(World, Start, End, Color, false, DebugDrawDuration, 0, 1.f);
		DrawDebugSphere(World, End, TraceRadius, 8, Color, false, DebugDrawDuration);
	}
#endif

	for (const FHitResult& Hit : Hits)
	{
		ProcessHit(MeshComp, Animation, Swing, Hit);
	}
}

void UANS_MeleeHitbox::ProcessHit(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, FSwingState& Swing, const FHitResult& Hit) const
{
	AActor* Owner = MeshComp->GetOwner();
	AActor* HitActor = Hit.GetActor();
	if (!HitActor || HitActor == Owner)
	{
		return;
	}

	if (bHitEachActorOnce)
	{
		if (Swing.HitActors.Contains(HitActor))
		{
			return;
		}
		Swing.HitActors.Add(HitActor);
	}

	FGameplayEventData Payload;
	Payload.Instigator = Owner;
	Payload.Target = HitActor;
	Payload.OptionalObject = Animation;
	Payload.EventMagnitude = DamageMultiplier;
	Payload.TargetData = UAbilitySystemBlueprintLibrary::AbilityTargetDataFromHitResult(Hit);

	// 1) Victim first, so an active parry can react before the attacker applies damage.
	if (UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(HitActor))
	{
		Payload.EventTag = TAG_Event_Combat_Hit;
		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(HitActor, TAG_Event_Combat_Hit, Payload);
	}

	// 2) Attacker: the running melee ability applies damage / posture from this.
	if (UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Owner))
	{
		Payload.EventTag = TAG_Event_Combat_HitDealt;
		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Owner, TAG_Event_Combat_HitDealt, Payload);
	}
}
