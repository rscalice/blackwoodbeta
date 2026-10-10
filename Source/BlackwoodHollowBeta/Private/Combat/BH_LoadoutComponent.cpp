// Blackwood Hollow - player weapon loadout component (implementation)

#include "Combat/BH_LoadoutComponent.h"
#include "Items/BH_WeaponItem.h"
#include "Items/BH_ArmorItem.h"
#include "Items/BH_EquipmentTypes.h"
#include "Combat/BH_StanceComponent.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystem/BH_CombatFunctionLibrary.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystem/Effects/AH_GE_CombatEffects.h"
#include "Progression/BH_RPGSettings.h"
#include "Player/BH_PlayerState.h"
#include "Actors/BH_IslandRules.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "EquipmentComponent.h"
#include "InventoryComponent.h"
#include "NarrativeItem.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY(LogBHLoadout);

namespace BH_LoadoutComponent_Private
{
	static FString SlotName(EBH_EquipSlot Slot)
	{
		const UEnum* Enum = StaticEnum<EBH_EquipSlot>();
		return Enum ? Enum->GetNameStringByValue(static_cast<int64>(Slot)) : FString();
	}

	/** Single source of truth for the stance rules (header comment): used by live items and by preset previews. */
	static FName StanceFromGrips(const UBH_WeaponItem* Main, const UBH_WeaponItem* Off)
	{
		if (!Main)
		{
			return NAME_None;
		}
		if (Main->GripType == EBH_WeaponGripType::TwoHanded)
		{
			return FName(TEXT("Greatsword"));
		}
		if (Off && Off->GripType == EBH_WeaponGripType::OneHanded)
		{
			return FName(TEXT("DualSword"));
		}
		// Off-hand item (shield), or main alone: sword-and-shield is the one-handed fallback stance.
		return FName(TEXT("SwordAndShield"));
	}

	static const UBH_WeaponItem* GetWeaponCDO(const UClass* ItemClass)
	{
		return ItemClass ? Cast<UBH_WeaponItem>(ItemClass->GetDefaultObject()) : nullptr;
	}

	/** Phase 12F: true while the owning pawn's player has weapon set B locked (ABH_PlayerState::IsWeaponSetBUnlocked). Any machine (the flag replicates). */
	static bool IsSetBLocked(const AActor* OwnerActor)
	{
		const APawn* OwnerPawn = Cast<APawn>(OwnerActor);
		const ABH_PlayerState* OwnerState = OwnerPawn ? Cast<ABH_PlayerState>(OwnerPawn->GetPlayerState()) : nullptr;
		return OwnerState && !OwnerState->IsWeaponSetBUnlocked();
	}
}

UBH_LoadoutComponent::UBH_LoadoutComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);

	// Default starter kit (see report): Longsword + KiteShield in A (-> SwordAndShield), Greatsword in B
	// (-> Greatsword), Shortsword carried but unequipped (equip it to Weapon_Off_A for DualSword).
	auto AddStarter = [this](const TCHAR* Path, bool bEquip, EBH_EquipSlot Slot)
	{
		FBH_StarterLoadoutEntry Entry;
		Entry.ItemClass = TSoftClassPtr<UBH_WeaponItem>(FSoftObjectPath(Path));
		Entry.bEquip = bEquip;
		Entry.Slot = Slot;
		StarterLoadout.Add(Entry);
	};
	AddStarter(TEXT("/Game/BlackwoodHollow/Items/Weapons/BPI_Weapon_Longsword.BPI_Weapon_Longsword_C"), true, EBH_EquipSlot::Weapon_Main_A);
	AddStarter(TEXT("/Game/BlackwoodHollow/Items/Weapons/BPI_Weapon_KiteShield.BPI_Weapon_KiteShield_C"), true, EBH_EquipSlot::Weapon_Off_A);
	AddStarter(TEXT("/Game/BlackwoodHollow/Items/Weapons/BPI_Weapon_Greatsword.BPI_Weapon_Greatsword_C"), true, EBH_EquipSlot::Weapon_Main_B);
	AddStarter(TEXT("/Game/BlackwoodHollow/Items/Weapons/BPI_Weapon_Shortsword.BPI_Weapon_Shortsword_C"), false, EBH_EquipSlot::Weapon_Off_A);
}

UBH_LoadoutComponent* UBH_LoadoutComponent::FindLoadoutComponent(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UBH_LoadoutComponent>() : nullptr;
}

void UBH_LoadoutComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	// Narrative's pawn-side equipment component (not replicated; it mirrors from the replicated items' bActive).
	Equipment = Owner->FindComponentByClass<UEquipmentComponent>();
	if (!Equipment)
	{
		Equipment = NewObject<UEquipmentComponent>(Owner, TEXT("Equipment"));
		Owner->AddInstanceComponent(Equipment);
		Equipment->RegisterComponent();
	}

	// No clothing slots on purpose: Narrative's clothing path (UEquippableItem_Clothing::HandleEquip) then never attaches anything to the hidden
	// gameplay mesh. Armor visuals are built by UBH_ArmorVisualComponent on the visible MetaHuman body. The leader pose component is only kept for API completeness.
	USkeletalMeshComponent* Leader = nullptr;
	if (ACharacter* Character = Cast<ACharacter>(Owner))
	{
		Leader = UBH_CombatFunctionLibrary::FindWeaponAttachMesh(Character, FName(TEXT("weapon_r_socket")));
	}
	Equipment->Initialize(TMap<EEquippableSlot, USkeletalMeshComponent*>(), Leader);
	Equipment->OnItemEquipped.AddDynamic(this, &UBH_LoadoutComponent::HandleItemEquipped);
	Equipment->OnItemUnequipped.AddDynamic(this, &UBH_LoadoutComponent::HandleItemUnequipped);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(ReconcileTimer, this, &UBH_LoadoutComponent::Reconcile, ReconcileInterval, true, 0.25f);
	}
}

void UBH_LoadoutComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ReconcileTimer);
		World->GetTimerManager().ClearTimer(PresetTimer);
	}
	if (Equipment)
	{
		Equipment->OnItemEquipped.RemoveDynamic(this, &UBH_LoadoutComponent::HandleItemEquipped);
		Equipment->OnItemUnequipped.RemoveDynamic(this, &UBH_LoadoutComponent::HandleItemUnequipped);
	}
	Super::EndPlay(EndPlayReason);
}

UNarrativeInventoryComponent* UBH_LoadoutComponent::GetInventory() const
{
	if (const APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		if (const APlayerState* PS = Pawn->GetPlayerState())
		{
			return PS->FindComponentByClass<UNarrativeInventoryComponent>();
		}
	}
	return nullptr;
}

UAbilitySystemComponent* UBH_LoadoutComponent::GetOwnerASC() const
{
	return UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner());
}

void UBH_LoadoutComponent::ForceInventoryNetUpdate() const
{
	if (const APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		if (APlayerState* PS = Pawn->GetPlayerState())
		{
			PS->ForceNetUpdate();
		}
	}
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

UBH_WeaponItem* UBH_LoadoutComponent::GetItemInSlot(EBH_EquipSlot Slot) const
{
	if (const UNarrativeInventoryComponent* Inventory = GetInventory())
	{
		for (UNarrativeItem* Item : Inventory->GetItems())
		{
			UBH_WeaponItem* Weapon = Cast<UBH_WeaponItem>(Item);
			if (Weapon && Weapon->bActive && Weapon->GetTargetSlot() == Slot)
			{
				return Weapon;
			}
		}
	}
	return nullptr;
}

bool UBH_LoadoutComponent::IsOffHandLocked(EBH_LoadoutSet Set) const
{
	const UBH_WeaponItem* Main = GetItemInSlot(UBH_EquipmentLibrary::GetMainSlot(Set));
	return Main && Main->GripType == EBH_WeaponGripType::TwoHanded;
}

FName UBH_LoadoutComponent::GetStanceForSet(EBH_LoadoutSet Set) const
{
	const UBH_WeaponItem* Main = GetItemInSlot(UBH_EquipmentLibrary::GetMainSlot(Set));
	return BH_LoadoutComponent_Private::StanceFromGrips(Main, Main ? GetItemInSlot(UBH_EquipmentLibrary::GetOffSlot(Set)) : nullptr);
}

FName UBH_LoadoutComponent::GetStanceNameForPreset(const TArray<FBH_StarterLoadoutEntry>& Entries, EBH_LoadoutSet Set)
{
	using namespace BH_LoadoutComponent_Private;

	const UBH_WeaponItem* Main = nullptr;
	const UBH_WeaponItem* Off = nullptr;
	for (const FBH_StarterLoadoutEntry& Entry : Entries)
	{
		if (!Entry.bEquip || !UBH_EquipmentLibrary::IsWeaponSlot(Entry.Slot) || UBH_EquipmentLibrary::GetSlotLoadoutSet(Entry.Slot) != Set)
		{
			continue;
		}
		const UBH_WeaponItem* CDO = GetWeaponCDO(Entry.ItemClass.LoadSynchronous());
		if (!CDO)
		{
			continue;
		}
		(UBH_EquipmentLibrary::IsOffHandSlot(Entry.Slot) ? Off : Main) = CDO;
	}
	return StanceFromGrips(Main, Off);
}

bool UBH_LoadoutComponent::GetActiveLoadoutSet(EBH_LoadoutSet& OutSet) const
{
	const FString Pose = UBH_CombatFunctionLibrary::GetCurrentOverlayPoseDisplayName(GetOwner());
	if (Pose.IsEmpty())
	{
		return false;
	}
	const FName PoseName(*Pose);
	for (EBH_LoadoutSet Set : { EBH_LoadoutSet::A, EBH_LoadoutSet::B })
	{
		if (GetStanceForSet(Set) == PoseName)
		{
			OutSet = Set;
			return true;
		}
	}
	return false;
}

// ---------------------------------------------------------------------------
// Equip / unequip (server)
// ---------------------------------------------------------------------------

bool UBH_LoadoutComponent::ValidateEquip(const UBH_WeaponItem* Item, EBH_EquipSlot Slot, FText& OutReason) const
{
	auto Fail = [&OutReason](const TCHAR* Message)
	{
		OutReason = FText::FromString(Message);
		return false;
	};

	if (!Item)
	{
		return Fail(TEXT("No item"));
	}
	if (!UBH_EquipmentLibrary::IsWeaponSlot(Slot))
	{
		return Fail(TEXT("Not a weapon slot"));
	}
	if (UBH_EquipmentLibrary::GetSlotLoadoutSet(Slot) == EBH_LoadoutSet::B && BH_LoadoutComponent_Private::IsSetBLocked(GetOwner()))
	{
		return Fail(TEXT("Weapon set B is locked"));
	}

	const bool bOffSlot = UBH_EquipmentLibrary::IsOffHandSlot(Slot);
	if (Item->GripType == EBH_WeaponGripType::TwoHanded && bOffSlot)
	{
		return Fail(TEXT("Two-handed weapons go in a main slot"));
	}
	if (Item->GripType == EBH_WeaponGripType::OffHand && !bOffSlot)
	{
		return Fail(TEXT("Off-hand items go in an off slot"));
	}

	if (bOffSlot)
	{
		const UBH_WeaponItem* Main = GetItemInSlot(UBH_EquipmentLibrary::GetMainSlot(UBH_EquipmentLibrary::GetSlotLoadoutSet(Slot)));
		if (Main && Main != Item && Main->GripType == EBH_WeaponGripType::TwoHanded)
		{
			return Fail(TEXT("Off hand is locked: a two-handed weapon is in the main slot of this set"));
		}
	}
	return true;
}

bool UBH_LoadoutComponent::EquipWeaponToSlot(UBH_WeaponItem* Item, EBH_EquipSlot Slot, FText& OutReason)
{
	using namespace BH_LoadoutComponent_Private;

	auto Fail = [&OutReason, Item, Slot](const TCHAR* Message)
	{
		OutReason = FText::FromString(Message);
		UE_LOG(LogBHLoadout, Log, TEXT("Equip %s -> %s rejected: %s"), Item ? *Item->GetFriendlyName() : TEXT("None"), *SlotName(Slot), Message);
		return false;
	};

	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return Fail(TEXT("Equipping is server-authoritative"));
	}
	if (!Item || !GetInventory() || Item->OwningInventory != GetInventory())
	{
		return Fail(TEXT("Item is not in this player's inventory"));
	}
	if (!ValidateEquip(Item, Slot, OutReason))
	{
		UE_LOG(LogBHLoadout, Log, TEXT("Equip %s -> %s rejected: %s"), *Item->GetFriendlyName(), *SlotName(Slot), *OutReason.ToString());
		return false;
	}

	const EBH_LoadoutSet Set = UBH_EquipmentLibrary::GetSlotLoadoutSet(Slot);
	UBH_WeaponItem* Occupant = GetItemInSlot(Slot);
	if (Occupant == Item)
	{
		OutReason = FText::GetEmpty();
		return true; // already there
	}

	// Free the target slot, and the off hand if this weapon is two-handed.
	if (Occupant)
	{
		Occupant->SetActive(false);
	}
	if (Item->GripType == EBH_WeaponGripType::TwoHanded)
	{
		if (UBH_WeaponItem* Off = GetItemInSlot(UBH_EquipmentLibrary::GetOffSlot(Set)))
		{
			if (Off != Item)
			{
				UE_LOG(LogBHLoadout, Log, TEXT("Two-handed %s auto-unequips %s from %s"), *Item->GetFriendlyName(), *Off->GetFriendlyName(), *SlotName(UBH_EquipmentLibrary::GetOffSlot(Set)));
				Off->SetActive(false);
			}
		}
	}

	const bool bWasActive = Item->bActive;
	if (bWasActive)
	{
		Item->SetActive(false);
	}
	if (!Item->AssignToSlot(Slot))
	{
		return Fail(TEXT("Slot does not fit this weapon's grip type"));
	}

	if (bWasActive)
	{
		// Moving an equipped weapon: the deactivate must replicate before the re-activate, so wait a beat.
		if (UWorld* World = GetWorld())
		{
			FTimerHandle Unused;
			World->GetTimerManager().SetTimer(Unused, FTimerDelegate::CreateUObject(this, &UBH_LoadoutComponent::ActivateDeferred, TWeakObjectPtr<UBH_WeaponItem>(Item)), 0.2f, false);
		}
	}
	else
	{
		Item->SetActive(true);
	}

	ForceInventoryNetUpdate();
	UE_LOG(LogBHLoadout, Log, TEXT("Equipped %s -> %s"), *Item->GetFriendlyName(), *SlotName(Slot));
	OutReason = FText::GetEmpty();
	EvaluateAvailableStances();
	RefreshStatMods();
	return true;
}

void UBH_LoadoutComponent::ActivateDeferred(TWeakObjectPtr<UBH_WeaponItem> Item)
{
	if (UBH_WeaponItem* Weapon = Item.Get())
	{
		Weapon->SetActive(true);
		ForceInventoryNetUpdate();
		EvaluateAvailableStances();
		RefreshStatMods();
	}
}

bool UBH_LoadoutComponent::UnequipWeaponSlot(EBH_EquipSlot Slot, FText& OutReason)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		OutReason = FText::FromString(TEXT("Unequipping is server-authoritative"));
		return false;
	}
	UBH_WeaponItem* Item = GetItemInSlot(Slot);
	if (!Item)
	{
		OutReason = FText::FromString(TEXT("Slot is empty"));
		return false;
	}
	Item->SetActive(false);
	ForceInventoryNetUpdate();
	UE_LOG(LogBHLoadout, Log, TEXT("Unequipped %s from %s"), *Item->GetFriendlyName(), *BH_LoadoutComponent_Private::SlotName(Slot));
	OutReason = FText::GetEmpty();
	EvaluateAvailableStances();
	RefreshStatMods();
	return true;
}

// ---------------------------------------------------------------------------
// Presets (server)
// ---------------------------------------------------------------------------

bool UBH_LoadoutComponent::ApplyLoadoutPreset(const TArray<FBH_StarterLoadoutEntry>& Entries, FText& OutReason)
{
	using namespace BH_LoadoutComponent_Private;

	auto Fail = [&OutReason](const FString& Message)
	{
		OutReason = FText::FromString(Message);
		UE_LOG(LogBHLoadout, Warning, TEXT("ApplyLoadoutPreset failed: %s"), *Message);
		return false;
	};

	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return Fail(TEXT("Applying a loadout preset is server-authoritative"));
	}
	if (Entries.Num() == 0)
	{
		return Fail(TEXT("Preset is empty"));
	}
	UNarrativeInventoryComponent* Inventory = GetInventory();
	if (!Inventory || !Equipment)
	{
		return Fail(TEXT("Inventory is not ready yet (PlayerState not assigned)"));
	}

	// -- 1. Validate everything before touching any state ----------------------------------------------------------
	TArray<UClass*> Classes;
	int32 UsedSlotMask = 0;
	const UBH_WeaponItem* MainCDO[2] = { nullptr, nullptr };
	bool bHasOff[2] = { false, false };
	for (const FBH_StarterLoadoutEntry& Entry : Entries)
	{
		UClass* ItemClass = Entry.ItemClass.LoadSynchronous();
		const UBH_WeaponItem* CDO = GetWeaponCDO(ItemClass);
		if (!CDO)
		{
			return Fail(FString::Printf(TEXT("Preset item class could not be loaded: %s"), *Entry.ItemClass.ToString()));
		}
		Classes.Add(ItemClass);
		if (!Entry.bEquip)
		{
			continue;
		}
		if (!UBH_EquipmentLibrary::IsWeaponSlot(Entry.Slot))
		{
			return Fail(FString::Printf(TEXT("%s: %s is not a weapon slot"), *ItemClass->GetName(), *SlotName(Entry.Slot)));
		}
		if (UBH_EquipmentLibrary::GetSlotLoadoutSet(Entry.Slot) == EBH_LoadoutSet::B && IsSetBLocked(GetOwner()))
		{
			return Fail(TEXT("Weapon set B is locked"));
		}
		const bool bOff = UBH_EquipmentLibrary::IsOffHandSlot(Entry.Slot);
		if ((CDO->GripType == EBH_WeaponGripType::TwoHanded && bOff) || (CDO->GripType == EBH_WeaponGripType::OffHand && !bOff))
		{
			return Fail(FString::Printf(TEXT("%s does not fit slot %s"), *ItemClass->GetName(), *SlotName(Entry.Slot)));
		}
		const int32 Bit = 1 << static_cast<int32>(Entry.Slot);
		if (UsedSlotMask & Bit)
		{
			return Fail(FString::Printf(TEXT("Slot %s is used twice in the preset"), *SlotName(Entry.Slot)));
		}
		UsedSlotMask |= Bit;

		const int32 SetIndex = static_cast<int32>(UBH_EquipmentLibrary::GetSlotLoadoutSet(Entry.Slot));
		if (bOff)
		{
			bHasOff[SetIndex] = true;
		}
		else
		{
			MainCDO[SetIndex] = CDO;
		}
	}
	for (int32 SetIndex = 0; SetIndex < 2; ++SetIndex)
	{
		if (bHasOff[SetIndex] && MainCDO[SetIndex] && MainCDO[SetIndex]->GripType == EBH_WeaponGripType::TwoHanded)
		{
			return Fail(TEXT("Preset puts an off-hand item under a two-handed main weapon"));
		}
	}

	// -- 2. One inventory instance per entry (reuse before granting; never one instance in two slots) ---------------
	TArray<UBH_WeaponItem*> Claimed;
	Claimed.Init(nullptr, Entries.Num());
	TSet<const UBH_WeaponItem*> Taken;

	auto FindUnclaimed = [&](const UClass* ItemClass, bool bWantActive) -> UBH_WeaponItem*
	{
		for (UNarrativeItem* Item : Inventory->GetItems())
		{
			UBH_WeaponItem* Weapon = Cast<UBH_WeaponItem>(Item);
			if (Weapon && Weapon->GetClass() == ItemClass && Weapon->bActive == bWantActive && !Taken.Contains(Weapon))
			{
				return Weapon;
			}
		}
		return nullptr;
	};

	// Pass A: an item already sitting in the entry's own slot stays put (no churn, no replication hop).
	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		if (!Entries[i].bEquip)
		{
			continue;
		}
		UBH_WeaponItem* Occupant = GetItemInSlot(Entries[i].Slot);
		if (Occupant && Occupant->GetClass() == Classes[i] && !Taken.Contains(Occupant))
		{
			Claimed[i] = Occupant;
			Taken.Add(Occupant);
		}
	}
	// Pass B: spare (unequipped) items first, then ones equipped elsewhere, then grant.
	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		if (Claimed[i])
		{
			continue;
		}
		UBH_WeaponItem* Weapon = FindUnclaimed(Classes[i], false);
		if (!Weapon)
		{
			Weapon = FindUnclaimed(Classes[i], true);
		}
		if (!Weapon)
		{
			const FItemAddResult Result = Inventory->TryAddItemFromClass(Classes[i], 1, /*bCheckAutoUse*/ false);
			Weapon = Result.Stacks.Num() > 0 ? Cast<UBH_WeaponItem>(Result.Stacks[0]) : nullptr;
			if (!Weapon)
			{
				return Fail(FString::Printf(TEXT("Could not grant %s (%s)"), *Classes[i]->GetName(), *Result.ErrorText.ToString()));
			}
			UE_LOG(LogBHLoadout, Log, TEXT("Preset: granted %s"), *Weapon->GetFriendlyName());
		}
		Claimed[i] = Weapon;
		Taken.Add(Weapon);
	}

	TArray<TPair<TWeakObjectPtr<UBH_WeaponItem>, EBH_EquipSlot>> Plan;
	for (int32 i = 0; i < Entries.Num(); ++i)
	{
		if (Entries[i].bEquip)
		{
			Plan.Emplace(Claimed[i], Entries[i].Slot);
		}
	}

	// -- 3a. Unequip every slot whose occupant is not the planned item -----------------------------------------------
	bool bAnyMoved = false;
	for (EBH_EquipSlot Slot : { EBH_EquipSlot::Weapon_Main_A, EBH_EquipSlot::Weapon_Off_A, EBH_EquipSlot::Weapon_Main_B, EBH_EquipSlot::Weapon_Off_B })
	{
		UBH_WeaponItem* Occupant = GetItemInSlot(Slot);
		if (!Occupant)
		{
			continue;
		}
		bool bKeep = false;
		for (const TPair<TWeakObjectPtr<UBH_WeaponItem>, EBH_EquipSlot>& Step : Plan)
		{
			bKeep |= (Step.Key.Get() == Occupant && Step.Value == Slot);
		}
		if (bKeep)
		{
			continue;
		}
		Occupant->SetActive(false);
		UE_LOG(LogBHLoadout, Log, TEXT("Preset: unequipped %s from %s"), *Occupant->GetFriendlyName(), *SlotName(Slot));
		// An instance that is also wanted in another slot has to replicate its deactivation before it is re-activated.
		bAnyMoved |= Claimed.Contains(Occupant);
	}
	ForceInventoryNetUpdate();

	// -- 3b / 4. Equip + stance (deferred when an instance is moving between slots) ----------------------------------
	UWorld* World = GetWorld();
	if (World)
	{
		World->GetTimerManager().ClearTimer(PresetTimer); // a newer preset supersedes a pending one
	}
	OutReason = FText::GetEmpty();

	if (bAnyMoved && World)
	{
		World->GetTimerManager().SetTimer(PresetTimer, FTimerDelegate::CreateWeakLambda(this, [this, Plan]()
		{
			FText Reason;
			if (!CommitPreset(Plan, Reason))
			{
				UE_LOG(LogBHLoadout, Warning, TEXT("Deferred preset commit failed: %s"), *Reason.ToString());
			}
		}), 0.25f, false);
		UE_LOG(LogBHLoadout, Log, TEXT("Preset accepted (%d entries); equip deferred 0.25s for replication"), Entries.Num());
		return true;
	}

	if (!CommitPreset(Plan, OutReason))
	{
		return Fail(OutReason.ToString());
	}
	UE_LOG(LogBHLoadout, Log, TEXT("Preset applied (%d entries)"), Entries.Num());
	return true;
}

bool UBH_LoadoutComponent::CommitPreset(const TArray<TPair<TWeakObjectPtr<UBH_WeaponItem>, EBH_EquipSlot>>& Plan, FText& OutReason)
{
	bool bAllOk = true;

	// Main slots first so an off-hand item is never validated against a stale two-handed main.
	for (const bool bOffPass : { false, true })
	{
		for (const TPair<TWeakObjectPtr<UBH_WeaponItem>, EBH_EquipSlot>& Step : Plan)
		{
			UBH_WeaponItem* Item = Step.Key.Get();
			if (!Item || UBH_EquipmentLibrary::IsOffHandSlot(Step.Value) != bOffPass)
			{
				continue;
			}
			FText Reason;
			if (!EquipWeaponToSlot(Item, Step.Value, Reason))
			{
				bAllOk = false;
				OutReason = Reason;
			}
		}
	}

	EvaluateAvailableStances();
	RefreshStatMods();

	// Switch to set A's stance (set B's when A is empty) so the swap is visible straight away. The server applies it;
	// the replicated stance + inventory carry it to clients.
	FName WantedStance = GetStanceForSet(EBH_LoadoutSet::A);
	if (WantedStance.IsNone())
	{
		WantedStance = GetStanceForSet(EBH_LoadoutSet::B);
	}
	if (!WantedStance.IsNone())
	{
		if (UBH_StanceComponent* StanceComp = UBH_StanceComponent::FindStanceComponent(GetOwner()))
		{
			const FGameplayTag StanceTag = BH_Stance::FromLegacyName(WantedStance);
			if (!StanceTag.IsValid() || !StanceComp->SetStance(StanceTag))
			{
				UE_LOG(LogBHLoadout, Warning, TEXT("Preset: stance '%s' could not be applied"), *WantedStance.ToString());
			}
		}
	}

	ForceInventoryNetUpdate();
	return bAllOk;
}

// ---------------------------------------------------------------------------
// Stances
// ---------------------------------------------------------------------------

void UBH_LoadoutComponent::EvaluateAvailableStances()
{
	TArray<FName> NewStances;
	for (EBH_LoadoutSet Set : { EBH_LoadoutSet::A, EBH_LoadoutSet::B })
	{
		const FName Stance = GetStanceForSet(Set);
		if (!Stance.IsNone())
		{
			NewStances.AddUnique(Stance);
		}
	}

	if (NewStances != AvailableStances)
	{
		AvailableStances = NewStances;
		UE_LOG(LogBHLoadout, Log, TEXT("%s AvailableStances changed (%d)"), *GetNameSafe(GetOwner()), AvailableStances.Num());
		OnAvailableStancesChanged.Broadcast(AvailableStances);
	}
}

void UBH_LoadoutComponent::HandleItemEquipped(const EEquippableSlot Slot, UEquippableItem* Equippable)
{
	EvaluateAvailableStances();
	RefreshStatMods();
}

void UBH_LoadoutComponent::HandleItemUnequipped(const EEquippableSlot Slot, UEquippableItem* Equippable)
{
	EvaluateAvailableStances();
	RefreshStatMods();
}

// ---------------------------------------------------------------------------
// Stat mods (server)
// ---------------------------------------------------------------------------

void UBH_LoadoutComponent::RefreshStatMods()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	RefreshArmorStatMods();
	UAbilitySystemComponent* ASC = GetOwnerASC();
	UNarrativeInventoryComponent* Inventory = GetInventory();
	if (!ASC || !Inventory)
	{
		return;
	}

	EBH_LoadoutSet ActiveSet = EBH_LoadoutSet::A;
	const bool bHasActiveSet = GetActiveLoadoutSet(ActiveSet);

	for (UNarrativeItem* Item : Inventory->GetItems())
	{
		UBH_WeaponItem* Weapon = Cast<UBH_WeaponItem>(Item);
		if (!Weapon)
		{
			continue;
		}

		const bool bShouldApply = Weapon->bActive && bHasActiveSet && Weapon->AssignedLoadout == ActiveSet;
		const bool bHasHandle = Weapon->StatModHandle.IsValid();

		if (bShouldApply && !bHasHandle)
		{
			Weapon->StatModHandle = UBH_EquipmentLibrary::ApplyStatMod(ASC, Weapon, Weapon->AttackPowerBonus, Weapon->DefenseBonus, Weapon->MaxStaminaBonus);
			UE_LOG(LogBHLoadout, Log, TEXT("Stat mod ON  %s (%s)"), *Weapon->GetFriendlyName(), *Weapon->DescribeBonuses());
		}
		else if (!bShouldApply && bHasHandle)
		{
			UBH_EquipmentLibrary::RemoveStatMod(ASC, Weapon->StatModHandle);
			UE_LOG(LogBHLoadout, Log, TEXT("Stat mod OFF %s"), *Weapon->GetFriendlyName());
		}
	}
}

void UBH_LoadoutComponent::RefreshArmorStatMods()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	UAbilitySystemComponent* ASC = GetOwnerASC();
	UNarrativeInventoryComponent* Inventory = GetInventory();
	if (!ASC)
	{
		return;
	}

	TSet<TWeakObjectPtr<UBH_ArmorItem>> Seen;
	if (Inventory)
	{
		for (UNarrativeItem* Item : Inventory->GetItems())
		{
			UBH_ArmorItem* Armor = Cast<UBH_ArmorItem>(Item);
			if (!Armor)
			{
				continue;
			}
			Seen.Add(Armor);

			FActiveGameplayEffectHandle* Existing = ArmorStatModHandles.Find(Armor);
			const bool bHasHandle = Existing && Existing->IsValid();
			if (Armor->bActive && !bHasHandle)
			{
				const FActiveGameplayEffectHandle Handle = UBH_EquipmentLibrary::ApplyStatMod(ASC, Armor, Armor->AttackPowerBonus, Armor->DefenseBonus, Armor->MaxStaminaBonus);
				ArmorStatModHandles.Add(Armor, Handle);
				UE_LOG(LogBHLoadout, Log, TEXT("Armor stat mod ON  %s (%s)"), *Armor->GetFriendlyName(), *Armor->DescribeBonuses());
			}
			else if (!Armor->bActive && Existing)
			{
				UBH_EquipmentLibrary::RemoveStatMod(ASC, *Existing);
				ArmorStatModHandles.Remove(Armor);
				UE_LOG(LogBHLoadout, Log, TEXT("Armor stat mod OFF %s"), *Armor->GetFriendlyName());
			}
		}
	}

	// Pieces that left the inventory (or were destroyed) while still applied: drop their effects so nothing leaks.
	for (auto It = ArmorStatModHandles.CreateIterator(); It; ++It)
	{
		if (!Seen.Contains(It.Key()))
		{
			UBH_EquipmentLibrary::RemoveStatMod(ASC, It.Value());
			It.RemoveCurrent();
		}
	}

	// Armor weight: ONE infinite effect for the heaviest equipped class (stamina regen multiplier + weight tag).
	// Re-applied only when the class changes; no armor equipped -> no effect.
	EBH_ArmorWeightClass Weight = EBH_ArmorWeightClass::Light;
	const bool bHasArmor = GetEquippedArmorWeight(Weight);
	const int32 DesiredIndex = bHasArmor ? static_cast<int32>(Weight) : -1;
	const bool bHandleAlive = ArmorWeightHandle.IsValid() && ASC->GetActiveGameplayEffect(ArmorWeightHandle) != nullptr;
	if (DesiredIndex != AppliedArmorWeightIndex || (DesiredIndex >= 0 && !bHandleAlive))
	{
		if (ArmorWeightHandle.IsValid())
		{
			ASC->RemoveActiveGameplayEffect(ArmorWeightHandle);
		}
		ArmorWeightHandle = FActiveGameplayEffectHandle();
		AppliedArmorWeightIndex = -1;

		if (bHasArmor)
		{
			TSubclassOf<UGameplayEffect> WeightEffect = UAH_GE_ArmorWeight::StaticClass();
			if (Weight == EBH_ArmorWeightClass::Medium)
			{
				WeightEffect = UAH_GE_ArmorWeight_Medium::StaticClass();
			}
			else if (Weight == EBH_ArmorWeightClass::Heavy)
			{
				WeightEffect = UAH_GE_ArmorWeight_Heavy::StaticClass();
			}

			const UBH_RPGSettings* Settings = UBH_RPGSettings::Get();
			FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
			FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(WeightEffect, 1.f, Context);
			if (Spec.IsValid() && Settings)
			{
				Spec.Data->SetSetByCallerMagnitude(TAG_Data_Equip_StaminaRegenMult, Settings->GetStaminaRegenMultiplier(Weight));
				ArmorWeightHandle = ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
				AppliedArmorWeightIndex = static_cast<int32>(Weight);
				UE_LOG(LogBHLoadout, Log, TEXT("Armor weight ON  class=%d regen x%.2f"), AppliedArmorWeightIndex, Settings->GetStaminaRegenMultiplier(Weight));
			}
		}
	}
}

bool UBH_LoadoutComponent::GetEquippedArmorWeight(EBH_ArmorWeightClass& OutWeight) const
{
	OutWeight = EBH_ArmorWeightClass::Light;
	const UNarrativeInventoryComponent* Inventory = GetInventory();
	if (!Inventory)
	{
		return false;
	}

	bool bAny = false;
	for (const UNarrativeItem* Item : Inventory->GetItems())
	{
		const UBH_ArmorItem* Armor = Cast<UBH_ArmorItem>(Item);
		if (!Armor || !Armor->bActive)
		{
			continue;
		}
		bAny = true;
		if (static_cast<uint8>(Armor->WeightClass) > static_cast<uint8>(OutWeight))
		{
			OutWeight = Armor->WeightClass;
		}
	}
	return bAny;
}

// ---------------------------------------------------------------------------
// Reconcile / starter kit
// ---------------------------------------------------------------------------

void UBH_LoadoutComponent::Reconcile()
{
	if (GetOwner() && GetOwner()->HasAuthority() && bGrantStarterLoadout && !bStarterGranted)
	{
		TryGrantStarterLoadout();
	}
	EvaluateAvailableStances();
	RefreshStatMods(); // follows the current overlay pose (stance changes are not otherwise observable here)
}

void UBH_LoadoutComponent::TryGrantStarterLoadout()
{
	UNarrativeInventoryComponent* Inventory = GetInventory();
	if (!Inventory || !Equipment)
	{
		return; // not a player pawn yet (or PlayerState not assigned): try again next reconcile
	}
	bStarterGranted = true;

	// Phase 12F: Island 1 starts the players unarmed (ABH_IslandRules::bStartUnarmed). The weapon cache grants the first weapon later.
	if (const ABH_IslandRules* Rules = ABH_IslandRules::FindRules(GetOwner()))
	{
		if (Rules->bStartUnarmed)
		{
			UE_LOG(LogBHLoadout, Log, TEXT("Starter loadout skipped: the island rules start the players unarmed"));
			return;
		}
	}

	// A respawned pawn re-uses the PlayerState inventory: do not hand the kit out twice.
	for (UNarrativeItem* Existing : Inventory->GetItems())
	{
		if (Cast<UBH_WeaponItem>(Existing))
		{
			UE_LOG(LogBHLoadout, Log, TEXT("Starter loadout skipped: inventory already holds weapons"));
			return;
		}
	}

	for (const FBH_StarterLoadoutEntry& Entry : StarterLoadout)
	{
		UClass* ItemClass = Entry.ItemClass.LoadSynchronous();
		if (!ItemClass)
		{
			UE_LOG(LogBHLoadout, Warning, TEXT("Starter loadout: could not load %s"), *Entry.ItemClass.ToString());
			continue;
		}

		const FItemAddResult Result = Inventory->TryAddItemFromClass(ItemClass, 1, /*bCheckAutoUse*/ false);
		UBH_WeaponItem* Weapon = Result.Stacks.Num() > 0 ? Cast<UBH_WeaponItem>(Result.Stacks[0]) : nullptr;
		if (!Weapon)
		{
			UE_LOG(LogBHLoadout, Warning, TEXT("Starter loadout: %s was not added (%s)"), *ItemClass->GetName(), *Result.ErrorText.ToString());
			continue;
		}

		if (Entry.bEquip)
		{
			FText Reason;
			EquipWeaponToSlot(Weapon, Entry.Slot, Reason);
		}
	}
	ForceInventoryNetUpdate();
}

// ---------------------------------------------------------------------------
// Debug
// ---------------------------------------------------------------------------

FString UBH_LoadoutComponent::DescribeState() const
{
	using namespace BH_LoadoutComponent_Private;

	FString Out;
	const UNarrativeInventoryComponent* Inventory = GetInventory();
	Out += FString::Printf(TEXT("Inventory items: %d\n"), Inventory ? Inventory->GetItems().Num() : -1);

	for (EBH_EquipSlot Slot : { EBH_EquipSlot::Weapon_Main_A, EBH_EquipSlot::Weapon_Off_A, EBH_EquipSlot::Weapon_Main_B, EBH_EquipSlot::Weapon_Off_B })
	{
		const UBH_WeaponItem* Item = GetItemInSlot(Slot);
		FString NarrativeSide = TEXT("-");
		if (Equipment)
		{
			if (UEquippableItem* NI = Equipment->GetEquippedItemAtSlot(UBH_EquipmentLibrary::ToEquippableSlot(Slot)))
			{
				NarrativeSide = Cast<UBH_WeaponItem>(NI) ? Cast<UBH_WeaponItem>(NI)->GetFriendlyName() : NI->GetClass()->GetName();
			}
		}
		const EBH_LoadoutSet Set = UBH_EquipmentLibrary::GetSlotLoadoutSet(Slot);
		const bool bLocked = UBH_EquipmentLibrary::IsOffHandSlot(Slot) && IsOffHandLocked(Set);
		Out += FString::Printf(TEXT("%s = %s | EquipmentComponent: %s%s\n"), *SlotName(Slot), Item ? *Item->GetFriendlyName() : TEXT("(empty)"), *NarrativeSide, bLocked ? TEXT(" | LOCKED") : TEXT(""));
	}

	FString Stances;
	for (const FName& S : AvailableStances)
	{
		Stances += (Stances.IsEmpty() ? TEXT("") : TEXT(", ")) + S.ToString();
	}
	EBH_LoadoutSet ActiveSet = EBH_LoadoutSet::A;
	const bool bHasSet = GetActiveLoadoutSet(ActiveSet);
	Out += FString::Printf(TEXT("AvailableStances = [%s] | Pose = %s | ActiveSet = %s\n"), *Stances, *UBH_CombatFunctionLibrary::GetCurrentOverlayPoseDisplayName(GetOwner()), bHasSet ? (ActiveSet == EBH_LoadoutSet::A ? TEXT("A") : TEXT("B")) : TEXT("none"));

	EBH_ArmorWeightClass ArmorWeight = EBH_ArmorWeightClass::Light;
	const bool bArmorWorn = GetEquippedArmorWeight(ArmorWeight);
	const TCHAR* ArmorWeightName = !bArmorWorn ? TEXT("none") : (ArmorWeight == EBH_ArmorWeightClass::Heavy) ? TEXT("Heavy") : (ArmorWeight == EBH_ArmorWeightClass::Medium) ? TEXT("Medium") : TEXT("Light");
	float RegenRate = 0.f;

	if (const UAbilitySystemComponent* ASC = GetOwnerASC())
	{
		if (const UAH_AttributeSet* Attr = ASC->GetSet<UAH_AttributeSet>())
		{
			RegenRate = Attr->GetStaminaRegenRate();
			Out += FString::Printf(TEXT("ArmorWeight = %s | StaminaRegenRate = %.2f\n"), ArmorWeightName, RegenRate);
			Out += FString::Printf(TEXT("AttackPower=%.1f Defense=%.1f MaxStamina=%.1f Stamina=%.1f MaxHealth=%.1f Health=%.1f"), Attr->GetAttackPower(), Attr->GetDefense(), Attr->GetMaxStamina(), Attr->GetStamina(), Attr->GetMaxHealth(), Attr->GetHealth());
		}
	}
	return Out;
}
