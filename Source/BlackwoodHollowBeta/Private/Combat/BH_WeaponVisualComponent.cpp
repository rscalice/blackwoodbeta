// Blackwood Hollow - weapon visuals on the MetaHuman visual body (implementation)

#include "Combat/BH_WeaponVisualComponent.h"
#include "AbilitySystem/BH_CombatFunctionLibrary.h"
#include "Animation/AnimInstance.h"
#include "Combat/BH_StanceComponent.h"
#include "Components/ChildActorComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInterface.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogBHWeaponVisual, Log, All);

UBH_WeaponVisualComponent::UBH_WeaponVisualComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork; // after the character meshes evaluated this frame
}

UBH_WeaponVisualComponent* UBH_WeaponVisualComponent::Find(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UBH_WeaponVisualComponent>() : nullptr;
}

void UBH_WeaponVisualComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() == NM_DedicatedServer)
	{
		SetComponentTickEnabled(false); // a dedicated server never draws anything
	}
}

void UBH_WeaponVisualComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (int32 Index = 0; Index < 2; ++Index)
	{
		ReleaseSlot(Index);
	}
	Super::EndPlay(EndPlayReason);
}

UMeshComponent* UBH_WeaponVisualComponent::GetVisualWeapon(EBH_WeaponSlot Slot) const
{
	return SlotStates[Slot == EBH_WeaponSlot::OffHand ? 1 : 0].Visual.Get();
}

USkeletalMeshComponent* UBH_WeaponVisualComponent::FindVisualBody() const
{
	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return nullptr;
	}
	static const FName VisualOverrideTag(TEXT("VisualOverride"));
	static const FName BodyComponentName(TEXT("Body"));

	TInlineComponentArray<UChildActorComponent*> ChildActorComponents(OwnerActor);
	for (UChildActorComponent* ChildComponent : ChildActorComponents)
	{
		if (!ChildComponent || !ChildComponent->ComponentHasTag(VisualOverrideTag))
		{
			continue;
		}
		AActor* VisualActor = ChildComponent->GetChildActor();
		if (!VisualActor)
		{
			continue;
		}
		TInlineComponentArray<USkeletalMeshComponent*> Meshes(VisualActor);
		for (USkeletalMeshComponent* Mesh : Meshes)
		{
			if (Mesh && Mesh->GetFName() == BodyComponentName)
			{
				return Mesh;
			}
		}
	}
	return nullptr;
}

void UBH_WeaponVisualComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	USkeletalMeshComponent* Body = FindVisualBody();
	if (VisualBody.Get() != Body)
	{
		// The visual actor appeared, was re-created, or is gone: clones belonged to the old one.
		for (int32 Index = 0; Index < 2; ++Index)
		{
			ReleaseSlot(Index);
		}
		VisualBody = Body;
		if (Body)
		{
			UE_LOG(LogBHWeaponVisual, Log, TEXT("%s: visual body found (%s), weapon visuals follow its sockets."), *GetNameSafe(GetOwner()), *GetNameSafe(Body));
		}
	}

	UpdateSlot(0, EBH_WeaponSlot::MainHand, Body);
	UpdateSlot(1, EBH_WeaponSlot::OffHand, Body);
	UpdateSwordArm(Body, DeltaTime);
}

void UBH_WeaponVisualComponent::UpdateSwordArm(USkeletalMeshComponent* Body, float DeltaTime)
{
	UAnimInstance* Anim = Body ? Body->GetAnimInstance() : nullptr;
	static const FName SwordArmAlphaName(TEXT("SwordArmAlpha"));
	// Blueprint "float" variables are doubles in UE5, so look the property up as a numeric one.
	const FNumericProperty* Property = Anim ? CastField<FNumericProperty>(Anim->GetClass()->FindPropertyByName(SwordArmAlphaName)) : nullptr;
	if (!Property)
	{
		return;
	}
	const UBH_StanceComponent* Stance = UBH_StanceComponent::FindStanceComponent(GetOwner());
	const float Target = (Stance && Stance->IsWeaponDrawn() && Stance->IsSwordVariant()) ? 1.f : 0.f;
	SwordArmAlpha = FMath::FInterpConstantTo(SwordArmAlpha, Target, DeltaTime, 1.f / FMath::Max(SwordArmBlendTime, 0.01f));
	Property->SetFloatingPointPropertyValue(Property->ContainerPtrToValuePtr<void>(Anim), static_cast<double>(SwordArmAlpha));
}

void UBH_WeaponVisualComponent::ReleaseSlot(int32 SlotIndex)
{
	FSlotState& State = SlotStates[SlotIndex];
	if (UMeshComponent* Clone = State.Visual.Get())
	{
		Clone->DestroyComponent();
	}
	if (UMeshComponent* Source = State.Source.Get())
	{
		Source->SetVisibility(true); // back to the old behaviour
	}
	State = FSlotState();
}

void UBH_WeaponVisualComponent::UpdateSlot(int32 SlotIndex, EBH_WeaponSlot Slot, USkeletalMeshComponent* Body)
{
	FSlotState& State = SlotStates[SlotIndex];
	UMeshComponent* Source = UBH_CombatFunctionLibrary::GetEquippedWeaponComponent(GetOwner(), Slot);

	if (!Source || !Body)
	{
		if (State.Visual.IsValid() || State.Source.IsValid())
		{
			ReleaseSlot(SlotIndex);
		}
		return;
	}

	// Which socket the source is on right now (hand, or the sheathed socket after the draw / sheath notify).
	const FName SocketName = Source->GetAttachSocketName();
	if (SocketName.IsNone() || !Body->DoesSocketExist(SocketName))
	{
		if (State.Visual.IsValid() || State.Source.IsValid())
		{
			ReleaseSlot(SlotIndex); // no matching socket on the visual body: the source weapon stays visible
		}
		return;
	}

	AActor* VisualActor = Body->GetOwner();
	UMeshComponent* Clone = State.Visual.Get();
	if (State.Source.Get() != Source || !Clone || Clone->GetOwner() != VisualActor)
	{
		ReleaseSlot(SlotIndex);
		Clone = nullptr;

		if (const USkeletalMeshComponent* SourceSkeletal = Cast<USkeletalMeshComponent>(Source))
		{
			USkeletalMeshComponent* NewSkeletal = NewObject<USkeletalMeshComponent>(VisualActor, NAME_None, RF_Transient);
			NewSkeletal->SetSkeletalMeshAsset(SourceSkeletal->GetSkeletalMeshAsset());
			Clone = NewSkeletal;
		}
		else if (const UStaticMeshComponent* SourceStatic = Cast<UStaticMeshComponent>(Source))
		{
			UStaticMeshComponent* NewStatic = NewObject<UStaticMeshComponent>(VisualActor, NAME_None, RF_Transient);
			NewStatic->SetStaticMesh(SourceStatic->GetStaticMesh());
			Clone = NewStatic;
		}
		if (!Clone)
		{
			return;
		}

		Clone->SetMobility(EComponentMobility::Movable);
		for (int32 MaterialIndex = 0; MaterialIndex < Source->GetNumMaterials(); ++MaterialIndex)
		{
			Clone->SetMaterial(MaterialIndex, Source->GetMaterial(MaterialIndex));
		}
		Clone->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Clone->SetGenerateOverlapEvents(false);
		Clone->SetCanEverAffectNavigation(false);
		Clone->SetReceivesDecals(false);
		Clone->SetCastShadow(Source->CastShadow);
		Clone->SetupAttachment(Body, SocketName);
		Clone->RegisterComponent();
		VisualActor->AddInstanceComponent(Clone);

		State.Source = Source;
		State.Visual = Clone;
		UE_LOG(LogBHWeaponVisual, Log, TEXT("%s: slot %d weapon visual built on socket %s."), *GetNameSafe(GetOwner()), SlotIndex, *SocketName.ToString());
	}

	// Follow the source: same socket, same relative transform (draw / sheath, two-hand aim and tuning all live in those two).
	if (Clone->GetAttachParent() != Body || Clone->GetAttachSocketName() != SocketName)
	{
		Clone->AttachToComponent(Body, FAttachmentTransformRules::SnapToTargetNotIncludingScale, SocketName);
	}
	const FTransform SourceRelative = Source->GetRelativeTransform();
	if (!Clone->GetRelativeTransform().Equals(SourceRelative, 0.001f))
	{
		Clone->SetRelativeTransform(SourceRelative);
	}
	Clone->SetVisibility(Body->IsVisible());
	if (Source->IsVisible())
	{
		Source->SetVisibility(false);
	}
}

bool UBH_WeaponVisualComponent::SampleDrift(EBH_WeaponSlot Slot, FVector& SourceWeapon, FVector& VisualWeapon, FVector& SourceHand, FVector& VisualHand) const
{
	const FSlotState& State = SlotStates[Slot == EBH_WeaponSlot::OffHand ? 1 : 0];
	const UMeshComponent* Source = State.Source.Get();
	const UMeshComponent* Clone = State.Visual.Get();
	const USkeletalMeshComponent* Body = VisualBody.Get();
	if (!Source || !Clone || !Body)
	{
		return false;
	}
	const FName HandBone = Slot == EBH_WeaponSlot::OffHand ? FName(TEXT("hand_l")) : FName(TEXT("hand_r"));
	SourceWeapon = Source->GetComponentLocation();
	VisualWeapon = Clone->GetComponentLocation();
	VisualHand = Body->GetBoneIndex(HandBone) != INDEX_NONE ? Body->GetBoneLocation(HandBone, EBoneSpaces::WorldSpace) : FVector::ZeroVector;
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const USkeletalMeshComponent* SourceMesh = Character ? Character->GetMesh() : nullptr;
	SourceHand = (SourceMesh && SourceMesh->GetBoneIndex(HandBone) != INDEX_NONE) ? SourceMesh->GetBoneLocation(HandBone, EBoneSpaces::WorldSpace) : FVector::ZeroVector;
	return true;
}
