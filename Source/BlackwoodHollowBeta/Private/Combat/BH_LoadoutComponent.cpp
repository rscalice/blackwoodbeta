// Blackwood Hollow - player weapon loadout component (implementation)

#include "Combat/BH_LoadoutComponent.h"
#include "Items/BH_WeaponItem.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystem/BH_CombatFunctionLibrary.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystem/Effects/AH_GE_CombatEffects.h"
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

	// No clothing meshes: armour visuals are out of scope. The visible mesh is the leader pose component.
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
	if (!Main)
	{
		return NAME_None;
	}
	if (Main->GripType == EBH_WeaponGripType::TwoHanded)
	{
		return FName(TEXT("Greatsword"));
	}

	const UBH_WeaponItem* Off = GetItemInSlot(UBH_EquipmentLibrary::GetOffSlot(Set));
	if (Off && Off->GripType == EBH_WeaponGripType::OneHanded)
	{
		return FName(TEXT("DualSword"));
	}
	// Off-hand item (shield), or main alone: sword-and-shield is the one-handed fallback stance.
	return FName(TEXT("SwordAndShield"));
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
			FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
			Context.AddSourceObject(Weapon);
			FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(UAH_GE_EquipmentStatMod::StaticClass(), 1.f, Context);
			if (Spec.IsValid())
			{
				Spec.Data->SetSetByCallerMagnitude(TAG_Data_Equip_AttackPower, Weapon->AttackPowerBonus);
				Spec.Data->SetSetByCallerMagnitude(TAG_Data_Equip_Defense, Weapon->DefenseBonus);
				Spec.Data->SetSetByCallerMagnitude(TAG_Data_Equip_MaxStamina, Weapon->MaxStaminaBonus);
				Weapon->StatModHandle = ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
				UE_LOG(LogBHLoadout, Log, TEXT("Stat mod ON  %s (%s)"), *Weapon->GetFriendlyName(), *Weapon->DescribeBonuses());
			}
		}
		else if (!bShouldApply && bHasHandle)
		{
			ASC->RemoveActiveGameplayEffect(Weapon->StatModHandle);
			Weapon->StatModHandle = FActiveGameplayEffectHandle();
			UE_LOG(LogBHLoadout, Log, TEXT("Stat mod OFF %s"), *Weapon->GetFriendlyName());
		}
	}
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

	if (const UAbilitySystemComponent* ASC = GetOwnerASC())
	{
		if (const UAH_AttributeSet* Attr = ASC->GetSet<UAH_AttributeSet>())
		{
			Out += FString::Printf(TEXT("AttackPower=%.1f Defense=%.1f MaxStamina=%.1f Stamina=%.1f MaxHealth=%.1f Health=%.1f"), Attr->GetAttackPower(), Attr->GetDefense(), Attr->GetMaxStamina(), Attr->GetStamina(), Attr->GetMaxHealth(), Attr->GetHealth());
		}
	}
	return Out;
}
