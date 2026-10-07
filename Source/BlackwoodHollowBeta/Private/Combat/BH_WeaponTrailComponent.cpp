// Blackwood Hollow - cosmetic weapon swing trail (implementation)

#include "Combat/BH_WeaponTrailComponent.h"
#include "AbilitySystem/BH_CombatFunctionLibrary.h"
#include "Animation/ANS_MeleeHitbox.h"
#include "Combat/BH_CombatFeel.h"
#include "Combat/BH_CombatIdentityComponent.h"
#include "Combat/BH_StanceComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"

UBH_WeaponTrailComponent::UBH_WeaponTrailComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork; // after the character's skeletal meshes evaluated this frame
	bAutoActivate = true;
}

UBH_WeaponTrailComponent* UBH_WeaponTrailComponent::FindOrCreate(AActor* Owner)
{
	UWorld* World = Owner ? Owner->GetWorld() : nullptr;
	if (!World || !World->IsGameWorld() || World->GetNetMode() == NM_DedicatedServer)
	{
		return nullptr;
	}
	if (UBH_WeaponTrailComponent* Existing = Owner->FindComponentByClass<UBH_WeaponTrailComponent>())
	{
		return Existing;
	}
	UBH_WeaponTrailComponent* Trail = NewObject<UBH_WeaponTrailComponent>(Owner, NAME_None, RF_Transient);
	Trail->RegisterComponent();
	return Trail;
}

void UBH_WeaponTrailComponent::ResolveTrailStyle(AActor* Owner, FChannel& Channel) const
{
	const UBH_CombatFeelSettings* Settings = UBH_CombatFeelSettings::Get();
	Channel.Material = nullptr;
	Channel.StyleKey = NAME_None;
	Channel.Lifetime = Settings->DefaultTrailLifetime;

	const UBH_CombatIdentityComponent* Identity = UBH_CombatIdentityComponent::Find(Owner);
	if (Identity && Identity->TrailMaterialOverride)
	{
		Channel.Material = Identity->TrailMaterialOverride;
		Channel.StyleKey = TEXT("Override");
		return;
	}

	FName Key = NAME_None;
	FGameplayTag StanceTag;
	if (Identity && Identity->OverlayMaterial && Identity->OverlayMaterial->GetName().Contains(TEXT("Echo")))
	{
		Key = TEXT("Echo");
	}
	else
	{
		const FString Pose = UBH_CombatFunctionLibrary::GetCurrentOverlayPoseDisplayName(Owner);
		Key = Pose.IsEmpty() ? FName(TEXT("SwordAndShield")) : FName(*Pose);
		if (UBH_StanceComponent::FindStanceComponent(Owner))
		{
			StanceTag = UBH_StanceComponent::GetStanceTagOf(Owner);
		}
	}

	// Tag-keyed tables first (Stance.Weapon.*), the legacy FName tables are the fallback.
	const TObjectPtr<UMaterialInterface>* Found = StanceTag.IsValid() ? Settings->TrailMaterialsByTag.Find(StanceTag) : nullptr;
	const float* Life = StanceTag.IsValid() ? Settings->TrailLifetimesByTag.Find(StanceTag) : nullptr;
	if (!Found || !*Found)
	{
		Found = Settings->TrailMaterials.Find(Key);
		if (!Found || !*Found)
		{
			Key = TEXT("SwordAndShield");
			Found = Settings->TrailMaterials.Find(Key);
		}
	}
	Channel.StyleKey = Key;
	Channel.Material = Found ? Found->Get() : nullptr;
	if (!Life)
	{
		Life = Settings->TrailLifetimes.Find(Key);
	}
	if (Life)
	{
		Channel.Lifetime = *Life;
	}
}

void UBH_WeaponTrailComponent::EnsureRibbon(FChannel& Channel)
{
	if (Channel.Ribbon)
	{
		return;
	}
	AActor* Owner = GetOwner();
	UProceduralMeshComponent* Ribbon = NewObject<UProceduralMeshComponent>(Owner, NAME_None, RF_Transient);
	Ribbon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ribbon->SetGenerateOverlapEvents(false);
	Ribbon->SetCanEverAffectNavigation(false);
	Ribbon->bUseComplexAsSimpleCollision = false;
	Ribbon->SetCastShadow(false);
	Ribbon->bReceivesDecals = false;
	Ribbon->bAffectDistanceFieldLighting = false;
	Ribbon->SetMobility(EComponentMobility::Movable);
	Ribbon->RegisterComponent();
	Ribbon->SetUsingAbsoluteLocation(true);
	Ribbon->SetUsingAbsoluteRotation(true);
	Ribbon->SetUsingAbsoluteScale(true);
	Ribbon->SetWorldTransform(FTransform::Identity); // vertices are written in world space
	Ribbon->SetVisibility(false);
	Channel.Ribbon = Ribbon;
}

void UBH_WeaponTrailComponent::BeginTrail(USkeletalMeshComponent* MeshComp, const UANS_MeleeHitbox* Hitbox)
{
	if (!MeshComp || !Hitbox)
	{
		return;
	}
	FChannel& Channel = Channels[Hitbox->WeaponSlot == EBH_WeaponSlot::OffHand ? 1 : 0];

	const bool bWasIdle = !Channel.bActive && Channel.Samples.Num() == 0;
	Channel.bActive = true;
	Channel.Source = MeshComp;
	Channel.Slot = Hitbox->WeaponSlot;
	Channel.RootSocket = Hitbox->RootSocketName;
	Channel.TipSocket = Hitbox->TipSocketName;
	Channel.bUseBounds = Hitbox->bUseWeaponBoundsIfSocketsMissing;
	if (bWasIdle || !Channel.Material)
	{
		ResolveTrailStyle(GetOwner(), Channel); // stance can change between swings; re-resolved whenever the trail starts fresh
	}
	if (!Channel.Material)
	{
		Channel.bActive = false; // no material mapped: no trail
		return;
	}
	EnsureRibbon(Channel);
	if (Channel.Ribbon)
	{
		Channel.Ribbon->SetMaterial(0, Channel.Material);
	}
	UpdateTickEnabled();
}

void UBH_WeaponTrailComponent::EndTrail(EBH_WeaponSlot Slot)
{
	Channels[Slot == EBH_WeaponSlot::OffHand ? 1 : 0].bActive = false; // samples age out over Lifetime
}

void UBH_WeaponTrailComponent::UpdateTickEnabled()
{
	bool bAny = false;
	for (const FChannel& Channel : Channels)
	{
		bAny |= Channel.bActive || Channel.Samples.Num() > 0 || Channel.bVisible;
	}
	SetComponentTickEnabled(bAny);
}

void UBH_WeaponTrailComponent::AddSample(FChannel& Channel, double Now) const
{
	USkeletalMeshComponent* Mesh = Channel.Source.Get();
	FVector Root, Tip;
	if (!Mesh || !UANS_MeleeHitbox::GetBladeRootTip(Mesh, Channel.Slot, Channel.RootSocket, Channel.TipSocket, Channel.bUseBounds, Root, Tip))
	{
		return;
	}
	Root = FMath::Lerp(Root, Tip, RootFraction);
	if (Channel.Samples.Num() > 0)
	{
		const FSample& Last = Channel.Samples.Last();
		if (FVector::DistSquared(Last.Root, Root) < 0.25 && FVector::DistSquared(Last.Tip, Tip) < 0.25)
		{
			return; // frozen (hit-stop): let the existing samples age out
		}
	}
	FSample Sample;
	Sample.Root = Root;
	Sample.Tip = Tip;
	Sample.Time = Now;
	Channel.Samples.Add(Sample);
	while (Channel.Samples.Num() > MaxSamples)
	{
		Channel.Samples.RemoveAt(0);
	}
}

void UBH_WeaponTrailComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;

	for (FChannel& Channel : Channels)
	{
		if (Channel.bActive)
		{
			AddSample(Channel, Now);
		}
		while (Channel.Samples.Num() > 0 && Now - Channel.Samples[0].Time > Channel.Lifetime)
		{
			Channel.Samples.RemoveAt(0);
		}

		if (Channel.Samples.Num() >= 2)
		{
			RebuildRibbon(Channel, Now);
		}
		else if (Channel.bVisible)
		{
			HideRibbon(Channel);
		}
	}
	UpdateTickEnabled();
}

void UBH_WeaponTrailComponent::HideRibbon(FChannel& Channel)
{
	if (Channel.Ribbon)
	{
		Channel.Ribbon->ClearMeshSection(0);
		Channel.Ribbon->SetVisibility(false);
	}
	Channel.bVisible = false;
	Channel.VertexCount = 0;
}

static FVector CatmullRom(const FVector& P0, const FVector& P1, const FVector& P2, const FVector& P3, float T)
{
	const float T2 = T * T;
	const float T3 = T2 * T;
	return 0.5f * ((2.f * P1) + (-P0 + P2) * T + (2.f * P0 - 5.f * P1 + 4.f * P2 - P3) * T2 + (-P0 + 3.f * P1 - 3.f * P2 + P3) * T3);
}

void UBH_WeaponTrailComponent::RebuildRibbon(FChannel& Channel, double Now)
{
	EnsureRibbon(Channel);
	if (!Channel.Ribbon)
	{
		return;
	}

	Vertices.Reset();
	Triangles.Reset();
	Normals.Reset();
	UVs.Reset();

	const TArray<FSample>& S = Channel.Samples;
	const int32 N = S.Num();
	const float Life = FMath::Max(Channel.Lifetime, 0.01f);

	auto EmitColumn = [&](const FVector& Root, const FVector& Tip, double Time)
	{
		const float Age = FMath::Clamp(static_cast<float>((Now - Time) / Life), 0.f, 1.f);
		Vertices.Add(Root);
		Vertices.Add(Tip);
		Normals.Add(FVector::UpVector);
		Normals.Add(FVector::UpVector);
		UVs.Add(FVector2D(Age, 0.f));
		UVs.Add(FVector2D(Age, 1.f));
		const int32 Columns = Vertices.Num() / 2;
		if (Columns >= 2)
		{
			const int32 A = (Columns - 2) * 2; // previous root
			const int32 B = A + 1;             // previous tip
			const int32 C = A + 2;             // this root
			const int32 D = A + 3;             // this tip
			Triangles.Append({ A, C, B, B, C, D });
		}
	};

	for (int32 I = 0; I < N - 1; ++I)
	{
		const FSample& P0 = S[FMath::Max(I - 1, 0)];
		const FSample& P1 = S[I];
		const FSample& P2 = S[I + 1];
		const FSample& P3 = S[FMath::Min(I + 2, N - 1)];

		const float Dist = static_cast<float>(FMath::Max(FVector::Dist(P1.Root, P2.Root), FVector::Dist(P1.Tip, P2.Tip)));
		const int32 Sub = FMath::Clamp(FMath::CeilToInt(Dist / FMath::Max(MaxSegmentLength, 1.f)), 1, MaxSubdivisions);
		for (int32 Step = 0; Step < Sub; ++Step)
		{
			const float T = static_cast<float>(Step) / static_cast<float>(Sub);
			EmitColumn(CatmullRom(P0.Root, P1.Root, P2.Root, P3.Root, T), CatmullRom(P0.Tip, P1.Tip, P2.Tip, P3.Tip, T), FMath::Lerp(P1.Time, P2.Time, static_cast<double>(T)));
		}
	}
	EmitColumn(S[N - 1].Root, S[N - 1].Tip, S[N - 1].Time);

	Channel.VertexCount = Vertices.Num();
	Channel.MaxVertexCount = FMath::Max(Channel.MaxVertexCount, Channel.VertexCount);
	const TArray<FProcMeshTangent> Tangents;
	Channel.Ribbon->CreateMeshSection(0, Vertices, Triangles, Normals, UVs, Colors, Tangents, /*bCreateCollision*/ false);
	Channel.Ribbon->SetMaterial(0, Channel.Material);
	if (!Channel.bVisible)
	{
		Channel.Ribbon->SetVisibility(true);
		Channel.bVisible = true;
	}
}

FString UBH_WeaponTrailComponent::DescribeTrails() const
{
	FString Out;
	for (int32 I = 0; I < 2; ++I)
	{
		const FChannel& C = Channels[I];
		Out += FString::Printf(TEXT("slot=%d active=%d visible=%d style=%s material=%s lifetime=%.2f samples=%d verts=%d maxVerts=%d\n"),
			I, C.bActive ? 1 : 0, C.bVisible ? 1 : 0, *C.StyleKey.ToString(), *GetNameSafe(C.Material), C.Lifetime, C.Samples.Num(), C.VertexCount, C.MaxVertexCount);
	}
	return Out;
}
