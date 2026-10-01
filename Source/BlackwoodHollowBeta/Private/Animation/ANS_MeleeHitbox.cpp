// Blackwood Hollow - Melee hitbox AnimNotifyState (implementation)

#include "Animation/ANS_MeleeHitbox.h"
#include "AbilitySystem/BH_CombatFunctionLibrary.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "Combat/BH_WeaponBladeData.h"
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
	// 2) Per-weapon blade line from the loadout (FBH_WeaponMeshSlot::bUseBladeOverride), component-local.
	else if (Weapon && Weapon->GetAssetUserData<UBH_WeaponBladeData>())
	{
		const UBH_WeaponBladeData* Blade = Weapon->GetAssetUserData<UBH_WeaponBladeData>();
		const FTransform& WeaponTransform = Weapon->GetComponentTransform();
		Root = WeaponTransform.TransformPosition(Blade->RootLocal);
		Tip = WeaponTransform.TransformPosition(Blade->TipLocal);
		bFound = true;
	}
	// 3) Longest axis of the weapon's local bounds.
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
	// 4) Same socket names on the animating character mesh (unarmed / debug setups).
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
	Swing.HitRecords.Reset();
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
		SweepMotion(MeshComp, Animation, *Swing, Swing->PreviousPoints, CurrentPoints);
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
	// Final sweep from the last ticked pose to the end pose. At low frame rates a
	// short window can begin and end within one frame with no NotifyTick at all.
	if (FSwingState* Swing = ActiveSwings.Find(MeshComp))
	{
		TArray<FVector> EndPoints;
		if (GetWeaponPoints(MeshComp, EndPoints) && Swing->PreviousPoints.Num() == EndPoints.Num())
		{
			SweepMotion(MeshComp, Animation, *Swing, Swing->PreviousPoints, EndPoints);
		}
	}

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

void UANS_MeleeHitbox::SweepMotion(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, FSwingState& Swing,
	const TArray<FVector>& Previous, const TArray<FVector>& Current) const
{
	const int32 NumPoints = Current.Num();
	if (NumPoints < 2 || Previous.Num() != NumPoints)
	{
		for (int32 Index = 0; Index < NumPoints && Index < Previous.Num(); ++Index)
		{
			SweepSegment(MeshComp, Animation, Swing, Previous[Index], Current[Index]);
		}
		return;
	}

	// Straight sweeps (chord) when arc sub-stepping is off, or the blade barely moved this tick.
	float MaxTravel = 0.f;
	for (int32 Index = 0; Index < NumPoints; ++Index)
	{
		MaxTravel = FMath::Max(MaxTravel, static_cast<float>(FVector::Dist(Previous[Index], Current[Index])));
	}

	if ((!bSubstepArc && !bAllowMultipleHits) || SubstepDistance <= 0.f || MaxTravel <= SubstepDistance)
	{
		for (int32 Index = 0; Index < NumPoints; ++Index)
		{
			SweepSegment(MeshComp, Animation, Swing, Previous[Index], Current[Index]);
		}
		return;
	}

	// Fast motion: interpolate the blade itself (root lerp, direction slerp, length lerp) and sweep
	// sub-step to sub-step, so a spin is covered along its arc instead of along the chord.
	constexpr int32 MaxSubsteps = 12;
	const int32 Substeps = FMath::Clamp(FMath::CeilToInt(MaxTravel / SubstepDistance), 2, MaxSubsteps);

	const FVector PrevRoot = Previous[0];
	const FVector CurRoot = Current[0];
	const FVector PrevVec = Previous.Last() - PrevRoot;
	const FVector CurVec = Current.Last() - CurRoot;
	const float PrevLen = static_cast<float>(PrevVec.Size());
	const float CurLen = static_cast<float>(CurVec.Size());
	const FVector PrevDir = PrevLen > KINDA_SMALL_NUMBER ? PrevVec / PrevLen : FVector::ForwardVector;
	const FVector CurDir = CurLen > KINDA_SMALL_NUMBER ? CurVec / CurLen : PrevDir;
	const FQuat Delta = FQuat::FindBetweenNormals(PrevDir, CurDir);

	TArray<FVector> StepStart = Previous;
	TArray<FVector> StepEnd;
	StepEnd.SetNum(NumPoints);
	for (int32 Step = 1; Step <= Substeps; ++Step)
	{
		const float StepAlpha = static_cast<float>(Step) / static_cast<float>(Substeps);
		const FVector Root = FMath::Lerp(PrevRoot, CurRoot, StepAlpha);
		const FVector Dir = FQuat::Slerp(FQuat::Identity, Delta, StepAlpha).RotateVector(PrevDir);
		const float Length = FMath::Lerp(PrevLen, CurLen, StepAlpha);
		for (int32 Index = 0; Index < NumPoints; ++Index)
		{
			const float PointAlpha = static_cast<float>(Index) / static_cast<float>(NumPoints - 1);
			StepEnd[Index] = Step == Substeps ? Current[Index] : Root + Dir * (Length * PointAlpha);
		}
		for (int32 Index = 0; Index < NumPoints; ++Index)
		{
			SweepSegment(MeshComp, Animation, Swing, StepStart[Index], StepEnd[Index]);
		}
		StepStart = StepEnd;
	}
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

	// Friendly fire: same-team targets are invisible to the blade.
	if (!bAllowFriendlyFire && UBH_CombatFunctionLibrary::AreCombatAllies(Owner, HitActor))
	{
		return;
	}

	// Dodge i-frames: an invulnerable victim is invisible to the blade. Checked before the hit bookkeeping so a
	// swing that is still active when the i-frames end can connect, and before any event so no hit reaction / cue plays.
	if (const UAbilitySystemComponent* VictimASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(HitActor))
	{
		if (VictimASC->HasMatchingGameplayTag(TAG_State_Combat_Invulnerable))
		{
			UE_LOG(LogBHCombat, Verbose, TEXT("MeleeHitbox: %s whiffed on %s (State.Combat.Invulnerable)."), *GetNameSafe(Owner), *GetNameSafe(HitActor));
			return;
		}
	}

	if (bAllowMultipleHits)
	{
		// Re-hit gate: measured in world time, so hit-stop / anim-rate changes can't cause double hits.
		const UWorld* World = MeshComp->GetWorld();
		const double Now = World ? World->GetTimeSeconds() : 0.0;
		FSwingState::FHitRecord& Record = Swing.HitRecords.FindOrAdd(HitActor);
		if (Record.HitCount > 0)
		{
			if (MaxHitsPerActor > 0 && Record.HitCount >= MaxHitsPerActor)
			{
				return;
			}
			if (Now - Record.LastHitTime < static_cast<double>(ReHitInterval))
			{
				return;
			}
		}
		Record.LastHitTime = Now;
		++Record.HitCount;
	}
	else if (bHitEachActorOnce)
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
