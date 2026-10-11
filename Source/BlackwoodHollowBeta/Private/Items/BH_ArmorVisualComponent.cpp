// Blackwood Hollow - Phase 11E armor visuals on the MetaHuman visual body (implementation)

#include "Items/BH_ArmorVisualComponent.h"
#include "Items/BH_ArmorItem.h"
#include "Items/BH_ArmorVisualTypes.h"
#include "Combat/BH_LoadoutComponent.h"
#include "Loot/BH_LootLibrary.h"
#include "Progression/BH_RPGSettings.h"
#include "InventoryComponent.h"
#include "NarrativeItem.h"
#include "Components/ChildActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogBHArmorVisual, Log, All);

namespace BH_ArmorVisualComponent_Private
{
	/** EBH_EquipSlot value -> slot index (0..4) or INDEX_NONE for weapon slots. */
	static int32 ToSlotIndex(EBH_EquipSlot Slot)
	{
		const int32 Index = static_cast<int32>(Slot);
		return (Index >= 0 && Index <= static_cast<int32>(EBH_EquipSlot::Feet)) ? Index : INDEX_NONE;
	}

	static const TCHAR* SlotLabel(int32 SlotIndex)
	{
		switch (SlotIndex)
		{
		case 0:  return TEXT("Head");
		case 1:  return TEXT("Chest");
		case 2:  return TEXT("Arms");
		case 3:  return TEXT("Legs");
		case 4:  return TEXT("Feet");
		default: return TEXT("?");
		}
	}
}

UBH_ArmorVisualComponent::UBH_ArmorVisualComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true); // nothing is replicated; this only lets the debug RPC route through the pawn's connection
	HairNameTokens.Add(TEXT("Hair"));
}

UBH_ArmorVisualComponent* UBH_ArmorVisualComponent::Find(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UBH_ArmorVisualComponent>() : nullptr;
}

void UBH_ArmorVisualComponent::BeginPlay()
{
	Super::BeginPlay();

	// A dedicated server never draws anything.
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(RefreshTimer, this, &UBH_ArmorVisualComponent::Refresh, RefreshInterval, true, 0.1f);
	}
}

void UBH_ArmorVisualComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshTimer);
	}
	// The piece components belong to the visual actor (a child actor of the pawn) and die with it.
	Super::EndPlay(EndPlayReason);
}

void UBH_ArmorVisualComponent::RefreshNow()
{
	Refresh();
}

UPrimitiveComponent* UBH_ArmorVisualComponent::GetSlotComponent(EBH_EquipSlot Slot) const
{
	const int32 Index = BH_ArmorVisualComponent_Private::ToSlotIndex(Slot);
	return Index == INDEX_NONE ? nullptr : SlotStates[Index].Component.Get();
}

USkeletalMeshComponent* UBH_ArmorVisualComponent::FindVisualBody() const
{
	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return nullptr;
	}

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

void UBH_ArmorVisualComponent::ResetAllSlots()
{
	for (int32 SlotIndex = 0; SlotIndex < NumVisualSlots; ++SlotIndex)
	{
		ClearSlot(SlotIndex);
	}
	AppliedBodyHidden.Reset();
	bHairStateValid = false;
}

void UBH_ArmorVisualComponent::ClearSlot(int32 SlotIndex)
{
	if (SlotIndex < 0 || SlotIndex >= NumVisualSlots)
	{
		return;
	}
	if (UPrimitiveComponent* Existing = SlotStates[SlotIndex].Component.Get())
	{
		Existing->DestroyComponent();
	}
	SlotStates[SlotIndex] = FSlotState();
}

void UBH_ArmorVisualComponent::Refresh()
{
	using namespace BH_ArmorVisualComponent_Private;

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	USkeletalMeshComponent* Body = FindVisualBody();
	if (!Body)
	{
		// The visual actor is gone (or not spawned yet): forget everything, it is rebuilt when it appears.
		if (VisualBody.IsValid())
		{
			ResetAllSlots();
			VisualBody.Reset();
		}
		return;
	}

	const bool bBodyChanged = VisualBody.Get() != Body;
	if (bBodyChanged)
	{
		// New (or re-created) visual body: old piece components were owned by the old actor.
		ResetAllSlots();
		VisualBody = Body;
		UE_LOG(LogBHArmorVisual, Log, TEXT("%s: visual body found (%s), building armor visuals."), *GetNameSafe(OwnerActor), *GetNameSafe(Body));
	}

	// -- Equipped armor per slot (replicated inventory, identical on every machine) ------------------------------
	const UBH_ArmorItem* Equipped[NumVisualSlots] = {};
	if (const UBH_LoadoutComponent* Loadout = UBH_LoadoutComponent::FindLoadoutComponent(OwnerActor))
	{
		if (const UNarrativeInventoryComponent* Inventory = Loadout->GetInventory())
		{
			for (const UNarrativeItem* Item : Inventory->GetItems())
			{
				const UBH_ArmorItem* Armor = Cast<UBH_ArmorItem>(Item);
				EBH_EquipSlot ArmorSlot = EBH_EquipSlot::Chest;
				if (Armor && Armor->bActive && Armor->GetArmorSlot(ArmorSlot))
				{
					const int32 Index = ToSlotIndex(ArmorSlot);
					if (Index != INDEX_NONE)
					{
						Equipped[Index] = Armor;
					}
				}
			}
		}
	}

	const UBH_RPGSettings* Settings = UBH_RPGSettings::Get();
	bool bAnyHidesHair = false;
	TSet<int32> DesiredBodyHidden;

	for (int32 SlotIndex = 0; SlotIndex < NumVisualSlots; ++SlotIndex)
	{
		const FBH_ArmorVisual* Visual = nullptr;
		const UObject* Source = nullptr;

		if (Equipped[SlotIndex] && Equipped[SlotIndex]->Visual.HasMesh())
		{
			Visual = &Equipped[SlotIndex]->Visual;
			Source = Equipped[SlotIndex];
		}
		else if (Settings)
		{
			Visual = Settings->FindStartingOutfitVisual(static_cast<EBH_EquipSlot>(SlotIndex));
			Source = Visual ? static_cast<const UObject*>(Settings) : nullptr;
		}

		FSlotState& State = SlotStates[SlotIndex];
		const bool bChanged = (State.Source.Get() != Source) || (!Source && State.Component.IsValid());
		if (bChanged)
		{
			ClearSlot(SlotIndex);
			if (Visual && Source)
			{
				BuildSlot(SlotIndex, Body, *Visual, Source);
			}
		}

		if (Visual)
		{
			bAnyHidesHair |= Visual->bHidesHair;
			for (const int32 BodySlot : Visual->HiddenBodyMaterialSlots)
			{
				DesiredBodyHidden.Add(BodySlot);
			}
		}
	}

	ApplyBodyHiddenSlots(Body, DesiredBodyHidden);

	if (!bHairStateValid || bHairHidden != bAnyHidesHair || bBodyChanged)
	{
		ApplyHairHidden(Body->GetOwner(), bAnyHidesHair);
		bHairHidden = bAnyHidesHair;
		bHairStateValid = true;
	}

	SyncCustomDepth(Body);
}

void UBH_ArmorVisualComponent::BuildSlot(int32 SlotIndex, USkeletalMeshComponent* Body, const FBH_ArmorVisual& Visual, const UObject* Source)
{
	using namespace BH_ArmorVisualComponent_Private;

	AActor* VisualActor = Body ? Body->GetOwner() : nullptr;
	if (!VisualActor || SlotIndex < 0 || SlotIndex >= NumVisualSlots)
	{
		return;
	}

	FSlotState& State = SlotStates[SlotIndex];
	State.Source = Source; // set even when the build fails so a bad asset is not retried every refresh

	UPrimitiveComponent* NewComponent = nullptr;

	USkeletalMesh* SkeletalAsset = Visual.SkeletalMesh.IsNull() ? nullptr : Visual.SkeletalMesh.LoadSynchronous();
	if (SkeletalAsset)
	{
		USkeletalMeshComponent* Piece = NewObject<USkeletalMeshComponent>(VisualActor, NAME_None, RF_Transient);
		Piece->SetMobility(EComponentMobility::Movable);
		Piece->SetSkeletalMeshAsset(SkeletalAsset);
		Piece->SetupAttachment(Body);
		Piece->RegisterComponent();
		VisualActor->AddInstanceComponent(Piece);
		// Bone-name based: the packs share the visual body's reference pose, so every bone of the piece copies the body's bone of that name.
		Piece->SetLeaderPoseComponent(Body);

		const int32 NumLods = FMath::Max(SkeletalAsset->GetLODNum(), 1);
		for (const int32 HiddenSlot : Visual.HiddenMaterialSlots)
		{
			if (HiddenSlot < 0)
			{
				continue;
			}
			for (int32 LodIndex = 0; LodIndex < NumLods; ++LodIndex)
			{
				// Material slot index == section index on these meshes (verified in the editor on the Villager mesh).
				Piece->ShowMaterialSection(HiddenSlot, HiddenSlot, false, LodIndex);
			}
		}
		NewComponent = Piece;
	}
	else if (UStaticMesh* StaticAsset = Visual.StaticMesh.IsNull() ? nullptr : Visual.StaticMesh.LoadSynchronous())
	{
		FTransform Relative = Visual.StaticMeshOffset;
		if (Visual.bStaticMeshInComponentSpace)
		{
			// The mesh sits at its authored place in the body's component space: remove the bone's reference pose so it follows the bone.
			if (const USkeletalMesh* BodyAsset = Body->GetSkeletalMeshAsset())
			{
				if (BodyAsset->GetRefSkeleton().FindBoneIndex(Visual.StaticMeshBone) != INDEX_NONE)
				{
					const FTransform BoneRefPose(BodyAsset->GetComposedRefPoseMatrix(Visual.StaticMeshBone));
					Relative = Visual.StaticMeshOffset * BoneRefPose.Inverse();
				}
				else
				{
					UE_LOG(LogBHArmorVisual, Warning, TEXT("Armor %s: bone '%s' not found on the visual body, attaching without the reference pose correction."), SlotLabel(SlotIndex), *Visual.StaticMeshBone.ToString());
				}
			}
		}

		UStaticMeshComponent* Piece = NewObject<UStaticMeshComponent>(VisualActor, NAME_None, RF_Transient);
		Piece->SetMobility(EComponentMobility::Movable);
		Piece->SetStaticMesh(StaticAsset);
		Piece->SetupAttachment(Body, Visual.StaticMeshBone);
		Piece->SetRelativeTransform(Relative);
		Piece->RegisterComponent();
		VisualActor->AddInstanceComponent(Piece);
		NewComponent = Piece;
	}
	else
	{
		UE_LOG(LogBHArmorVisual, Warning, TEXT("%s armor visual: mesh could not be loaded (skeletal '%s', static '%s')."), SlotLabel(SlotIndex), *Visual.SkeletalMesh.ToString(), *Visual.StaticMesh.ToString());
		return;
	}

	// Material overrides.
	for (const FBH_ArmorMaterialOverride& MaterialOverride : Visual.MaterialOverrides)
	{
		if (MaterialOverride.SlotIndex < 0 || MaterialOverride.Material.IsNull())
		{
			continue;
		}
		if (UMaterialInterface* Material = MaterialOverride.Material.LoadSynchronous())
		{
			NewComponent->SetMaterial(MaterialOverride.SlotIndex, Material);
		}
	}

	// Cosmetic only: shadows on, no collision / overlaps / navigation influence, no telegraph decals tinting the armor.
	NewComponent->SetCastShadow(true);
	NewComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	NewComponent->SetGenerateOverlapEvents(false);
	NewComponent->SetCanEverAffectNavigation(false);
	NewComponent->SetReceivesDecals(false);

	State.Component = NewComponent;
	UE_LOG(LogBHArmorVisual, Log, TEXT("%s: %s armor built (%s)."), *GetNameSafe(GetOwner()), SlotLabel(SlotIndex), *GetNameSafe(NewComponent));
}

void UBH_ArmorVisualComponent::ApplyHairHidden(AActor* VisualActor, bool bHide)
{
	if (!VisualActor)
	{
		return;
	}
	TInlineComponentArray<UPrimitiveComponent*> Primitives(VisualActor);
	for (UPrimitiveComponent* Primitive : Primitives)
	{
		if (!Primitive || !Primitive->GetClass()->GetName().Contains(TEXT("Groom")))
		{
			continue;
		}
		const FString ComponentName = Primitive->GetName();
		for (const FString& Token : HairNameTokens)
		{
			if (!Token.IsEmpty() && ComponentName.Contains(Token, ESearchCase::IgnoreCase))
			{
				Primitive->SetHiddenInGame(bHide);
				break;
			}
		}
	}
}

void UBH_ArmorVisualComponent::ApplyBodyHiddenSlots(USkeletalMeshComponent* Body, const TSet<int32>& Desired)
{
	if (!Body)
	{
		return;
	}
	const USkeletalMesh* BodyAsset = Body->GetSkeletalMeshAsset();
	const int32 NumLods = BodyAsset ? FMath::Max(BodyAsset->GetLODNum(), 1) : 1;

	for (const int32 BodySlot : Desired)
	{
		if (BodySlot >= 0 && !AppliedBodyHidden.Contains(BodySlot))
		{
			for (int32 LodIndex = 0; LodIndex < NumLods; ++LodIndex)
			{
				Body->ShowMaterialSection(BodySlot, BodySlot, false, LodIndex);
			}
		}
	}
	for (const int32 BodySlot : AppliedBodyHidden)
	{
		if (!Desired.Contains(BodySlot))
		{
			for (int32 LodIndex = 0; LodIndex < NumLods; ++LodIndex)
			{
				Body->ShowMaterialSection(BodySlot, BodySlot, true, LodIndex);
			}
		}
	}
	AppliedBodyHidden = Desired;
}

void UBH_ArmorVisualComponent::SyncCustomDepth(const USkeletalMeshComponent* Body) const
{
	if (!Body)
	{
		return;
	}
	for (const FSlotState& State : SlotStates)
	{
		UPrimitiveComponent* Piece = State.Component.Get();
		if (!Piece)
		{
			continue;
		}
		if (Piece->bRenderCustomDepth != Body->bRenderCustomDepth)
		{
			Piece->SetRenderCustomDepth(Body->bRenderCustomDepth);
		}
		if (Piece->CustomDepthStencilValue != Body->CustomDepthStencilValue)
		{
			Piece->SetCustomDepthStencilValue(Body->CustomDepthStencilValue);
		}
	}
}

// ---------------------------------------------------------------------------
// Debug: bh.Armor.Give
// ---------------------------------------------------------------------------

bool UBH_ArmorVisualComponent::ServerDebugArmorSet_Validate(const FString& SetName)
{
#if UE_BUILD_SHIPPING
	return false;
#else
	return SetName.Len() <= 32;
#endif
}

void UBH_ArmorVisualComponent::ServerDebugArmorSet_Implementation(const FString& SetName)
{
#if !UE_BUILD_SHIPPING
	APawn* PawnOwner = Cast<APawn>(GetOwner());
	if (!PawnOwner || !PawnOwner->HasAuthority())
	{
		return;
	}
	APlayerState* PlayerStateOwner = PawnOwner->GetPlayerState();
	const UBH_LoadoutComponent* Loadout = UBH_LoadoutComponent::FindLoadoutComponent(PawnOwner);
	UNarrativeInventoryComponent* Inventory = Loadout ? Loadout->GetInventory() : nullptr;
	if (!PlayerStateOwner || !Inventory)
	{
		UE_LOG(LogBHArmorVisual, Warning, TEXT("bh.Armor.Give: %s has no PlayerState inventory yet."), *GetNameSafe(PawnOwner));
		return;
	}

	TArray<TSubclassOf<UBH_ArmorItem>> Wanted;
	const bool bClear = SetName.Equals(TEXT("Clear"), ESearchCase::IgnoreCase);
	if (!bClear)
	{
		const UBH_RPGSettings* Settings = UBH_RPGSettings::Get();
		const FBH_ArmorDebugSet* DebugSet = Settings ? Settings->FindDebugArmorSet(SetName) : nullptr;
		if (!DebugSet)
		{
			UE_LOG(LogBHArmorVisual, Warning, TEXT("bh.Armor.Give: unknown set '%s' (use Light, Medium, Heavy or Clear)."), *SetName);
			return;
		}
		for (const TSoftClassPtr<UBH_ArmorItem>& Soft : DebugSet->Pieces)
		{
			if (UClass* Loaded = Soft.LoadSynchronous())
			{
				Wanted.Add(Loaded);
			}
			else
			{
				UE_LOG(LogBHArmorVisual, Warning, TEXT("bh.Armor.Give: item class %s could not be loaded (is the item Blueprint created?)."), *Soft.ToString());
			}
		}
	}

	// 1) Take off every armor piece that is not part of the wanted set.
	for (UNarrativeItem* Item : Inventory->GetItems())
	{
		UBH_ArmorItem* Armor = Cast<UBH_ArmorItem>(Item);
		if (!Armor || !Armor->bActive)
		{
			continue;
		}
		bool bInWantedSet = false;
		for (const TSubclassOf<UBH_ArmorItem>& WantedClass : Wanted)
		{
			if (WantedClass.Get() == Armor->GetClass())
			{
				bInWantedSet = true;
				break;
			}
		}
		if (!bInWantedSet)
		{
			Armor->SetActive(false);
		}
	}

	// 2) Grant what is missing (reuse existing instances, so repeated use never piles items up) and put it on.
	for (const TSubclassOf<UBH_ArmorItem>& PieceClass : Wanted)
	{
		auto FindPiece = [Inventory, &PieceClass]() -> UBH_ArmorItem*
		{
			for (UNarrativeItem* Item : Inventory->GetItems())
			{
				if (Item && Item->GetClass() == PieceClass.Get())
				{
					return Cast<UBH_ArmorItem>(Item);
				}
			}
			return nullptr;
		};

		UBH_ArmorItem* Piece = FindPiece();
		if (!Piece)
		{
			UBH_LootLibrary::GrantItem(PlayerStateOwner, PieceClass, 1, /*bNotifyPlayer*/ false);
			Piece = FindPiece();
		}
		if (Piece && !Piece->bActive)
		{
			Piece->SetActive(true);
		}
		else if (!Piece)
		{
			UE_LOG(LogBHArmorVisual, Warning, TEXT("bh.Armor.Give: %s could not be added to the inventory."), *GetNameSafe(PieceClass.Get()));
		}
	}

	PlayerStateOwner->ForceNetUpdate();
	UE_LOG(LogBHArmorVisual, Log, TEXT("bh.Armor.Give %s: %d piece(s) equipped on %s."), *SetName, Wanted.Num(), *GetNameSafe(PawnOwner));
#else
	(void)SetName;
#endif
}
