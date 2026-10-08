// Blackwood Hollow - Warden's Incense sanctuary (implementation)

#include "Consumables/BH_WardenSanctuary.h"
#include "Consumables/BH_ConsumableEffects.h"
#include "AbilitySystem/BH_CombatFunctionLibrary.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystem/StatusEffects/BH_BlightEffects.h"
#include "Combat/BH_CombatTeam.h"
#include "Components/BPC_HeartFragment.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Engine/CollisionProfile.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

ABH_WardenSanctuary::ABH_WardenSanctuary()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(false);
	SetNetUpdateFrequency(2.f);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	RingMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RingMesh"));
	RingMesh->SetupAttachment(SceneRoot);
	RingMesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	RingMesh->SetGenerateOverlapEvents(false);
	RingMesh->SetCastShadow(false);
	RingMesh->SetRelativeLocation(FVector(0.f, 0.f, 3.f));

	// Placeholder visual: the engine cylinder, flattened to a disc in ApplyRingVisual.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderFinder.Succeeded())
	{
		RingMesh->SetStaticMesh(CylinderFinder.Object);
	}
}

void ABH_WardenSanctuary::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(ABH_WardenSanctuary, Radius, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(ABH_WardenSanctuary, Duration, COND_InitialOnly);
}

void ABH_WardenSanctuary::BeginPlay()
{
	Super::BeginPlay();

	ApplyRingVisual();

	if (HasAuthority())
	{
		SetLifeSpan(FMath::Max(Duration, 0.5f)); // destroys the actor on the server; the destruction replicates
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(UpdateTimer, this, &ABH_WardenSanctuary::UpdateMembers, FMath::Max(UpdateInterval, 0.05f), true, 0.f);
		}
	}
}

void ABH_WardenSanctuary::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority())
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(UpdateTimer);
		}
		RemoveAllMembers();
	}
	Super::EndPlay(EndPlayReason);
}

void ABH_WardenSanctuary::OnRep_Radius()
{
	ApplyRingVisual();
}

void ABH_WardenSanctuary::ApplyRingVisual()
{
	if (!RingMesh)
	{
		return;
	}
	// The engine cylinder is 100 cm across and 100 cm tall: XY = diameter / 100, Z = 2 cm.
	const float Diameter = FMath::Max(Radius, 50.f) * 2.f;
	RingMesh->SetRelativeScale3D(FVector(Diameter / 100.f, Diameter / 100.f, 0.02f));

	if (UMaterialInterface* BaseMaterial = RingMesh->GetMaterial(0))
	{
		if (UMaterialInstanceDynamic* Dynamic = RingMesh->CreateDynamicMaterialInstance(0, BaseMaterial))
		{
			Dynamic->SetVectorParameterValue(TEXT("Color"), RingColor); // BasicShapeMaterial's tint parameter (no-op on other materials)
		}
	}
}

// ============================================================================
// Server: membership
// ============================================================================

void ABH_WardenSanctuary::UpdateMembers()
{
	UWorld* World = GetWorld();
	if (!World || !HasAuthority())
	{
		return;
	}

	// Living allies (combat team Players) whose capsule touches the sphere.
	TSet<APawn*> Inside;
	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(WardenSanctuaryOverlap), /*bTraceComplex*/ false);
	World->OverlapMultiByObjectType(Overlaps, GetActorLocation(), FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn),
		FCollisionShape::MakeSphere(FMath::Max(Radius, 50.f)), QueryParams);
	for (const FOverlapResult& Overlap : Overlaps)
	{
		APawn* Candidate = Cast<APawn>(Overlap.GetActor());
		if (!Candidate || UBH_CombatFunctionLibrary::GetCombatTeam(Candidate) != EBH_CombatTeam::Players)
		{
			continue;
		}
		const UAbilitySystemComponent* CandidateASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Candidate);
		if (!CandidateASC || CandidateASC->HasMatchingGameplayTag(TAG_State_Combat_Dead))
		{
			continue;
		}
		Inside.Add(Candidate);
	}

	// Leavers (or the dead / destroyed).
	TArray<TWeakObjectPtr<APawn>> Leavers;
	for (const TPair<TWeakObjectPtr<APawn>, FActiveGameplayEffectHandle>& Pair : Members)
	{
		APawn* Member = Pair.Key.Get();
		if (!Member || !Inside.Contains(Member))
		{
			Leavers.Add(Pair.Key);
		}
	}
	for (const TWeakObjectPtr<APawn>& Leaver : Leavers)
	{
		RemoveMember(Leaver.Get());
		Members.Remove(Leaver);
	}

	// Joiners, then the drain for everybody inside.
	const float Drain = BlightDrainPerSecond * FMath::Max(UpdateInterval, 0.05f);
	for (APawn* Member : Inside)
	{
		if (!Members.Contains(Member))
		{
			AddMember(Member);
		}
		if (UBPC_HeartFragment* Heart = Member->FindComponentByClass<UBPC_HeartFragment>())
		{
			Heart->ReduceBlightBuildup(Drain);
		}
	}
}

void ABH_WardenSanctuary::AddMember(APawn* Pawn)
{
	if (!Pawn)
	{
		return;
	}
	UAbilitySystemComponent* MemberASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Pawn);
	if (!MemberASC)
	{
		return;
	}

	// 1) Blight Rot is cleansed on entry (only on entry: a Rot that lands later is not removed again).
	UAH_GE_BlightRot::RemoveBlightRot(MemberASC);

	// 2) Blight build-up x (1 - BlightReductionFraction) while inside.
	if (UBPC_HeartFragment* Heart = Pawn->FindComponentByClass<UBPC_HeartFragment>())
	{
		Heart->AddBlightReductionSource(this, BlightReductionFraction);
	}

	// 3) Posture regen x PostureRegenMultiplier (also grants State.Status.WardenSanctuary for the HUD).
	Members.Add(Pawn, UBH_GE_WardenSanctuaryBuff::Apply(MemberASC, PostureRegenMultiplier));
}

void ABH_WardenSanctuary::RemoveMember(APawn* Pawn)
{
	if (!Pawn)
	{
		return;
	}
	if (UBPC_HeartFragment* Heart = Pawn->FindComponentByClass<UBPC_HeartFragment>())
	{
		Heart->RemoveBlightReductionSource(this);
	}
	if (const FActiveGameplayEffectHandle* Handle = Members.Find(Pawn))
	{
		if (Handle->IsValid())
		{
			if (UAbilitySystemComponent* MemberASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Pawn))
			{
				MemberASC->RemoveActiveGameplayEffect(*Handle);
			}
		}
	}
}

void ABH_WardenSanctuary::RemoveAllMembers()
{
	TArray<TWeakObjectPtr<APawn>> All;
	Members.GetKeys(All);
	for (const TWeakObjectPtr<APawn>& Key : All)
	{
		RemoveMember(Key.Get());
	}
	Members.Reset();
}
