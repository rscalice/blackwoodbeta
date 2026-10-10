// Blackwood Hollow - stance radial menu (implementation)

#include "UI/BH_StanceRadialComponent.h"
#include "Combat/BH_LoadoutComponent.h"
#include "Combat/BH_StanceComponent.h"
#include "AbilitySystem/BH_CombatFunctionLibrary.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "Consumables/BH_ConsumableLibrary.h"
#include "Loot/BH_LootLibrary.h"
#include "Loot/BH_LootTypes.h"
#include "Player/BH_PlayerState.h"
#include "NarrativeItem.h"
#include "GameFramework/PlayerState.h"
#include "RadialSelectorType.h"
#include "RadialSelectorMenuLayout.h"
#include "EnhancedInputComponent.h"
#include "InputAction.h"
#include "Abilities/GameplayAbility.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"

UBH_StanceRadialComponent::UBH_StanceRadialComponent()
{
	StanceDisplayNames.Add(FName(TEXT("SwordAndShield")), NSLOCTEXT("BHStance", "SnS", "Sword & Shield"));
	StanceDisplayNames.Add(FName(TEXT("DualSword")), NSLOCTEXT("BHStance", "DS", "Dual Swords"));
	StanceDisplayNames.Add(FName(TEXT("Greatsword")), NSLOCTEXT("BHStance", "GS", "Greatsword"));

	StanceDisplayNamesByTag.Add(TAG_Stance_Weapon_SwordShield.GetTag(), NSLOCTEXT("BHStance", "SnS", "Sword & Shield"));
	StanceDisplayNamesByTag.Add(TAG_Stance_Weapon_DualSword.GetTag(), NSLOCTEXT("BHStance", "DS", "Dual Swords"));
	StanceDisplayNamesByTag.Add(TAG_Stance_Weapon_Greatsword.GetTag(), NSLOCTEXT("BHStance", "GS", "Greatsword"));

	StanceDisplayNames.Add(FName(TEXT("OneHandedSword")), NSLOCTEXT("BHStance", "Name_1H", "One-Handed Sword"));
	StanceDisplayNames.Add(FName(TEXT("Bow")), NSLOCTEXT("BHStance", "Name_Bow", "Bow"));
	StanceDisplayNames.Add(FName(TEXT("Crossbow")), NSLOCTEXT("BHStance", "Name_Crossbow", "Crossbow"));
	StanceDisplayNamesByTag.Add(TAG_Stance_Weapon_OneHandedSword.GetTag(), NSLOCTEXT("BHStance", "Name_1H", "One-Handed Sword"));
	StanceDisplayNamesByTag.Add(TAG_Stance_Weapon_Bow.GetTag(), NSLOCTEXT("BHStance", "Name_Bow", "Bow"));
	StanceDisplayNamesByTag.Add(TAG_Stance_Weapon_Crossbow.GetTag(), NSLOCTEXT("BHStance", "Name_Crossbow", "Crossbow"));

	ConsumableSlots.SetNum(NumConsumableSlots);

	// Hold-to-open, release-to-confirm (we drive open/close ourselves; see header).
	ActivationMode = ERadialSelectorActivationMode::Hold;
	ConfirmTrigger = ERadialSelectorConfirmTrigger::OnActivationInput;
}

void UBH_StanceRadialComponent::BeginPlay()
{
	ConsumableSlots.SetNum(NumConsumableSlots);
	OnInputBound.AddUniqueDynamic(this, &UBH_StanceRadialComponent::HandleInputBound);
	OnSegmentSelected.AddUniqueDynamic(this, &UBH_StanceRadialComponent::HandleSegmentSelected);

	// The base component wants MenuData while it initialises; start with the default cycle and rebuild once a pawn is bound.
	if (!MenuData)
	{
		RebuildFromStances({ BH_Stance::ToLegacyName(TAG_Stance_Weapon_SwordShield.GetTag()), BH_Stance::ToLegacyName(TAG_Stance_Weapon_DualSword.GetTag()),
			BH_Stance::ToLegacyName(TAG_Stance_Weapon_Greatsword.GetTag()) });
	}

	Super::BeginPlay();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(PollTimer, this, &UBH_StanceRadialComponent::PollPawn, PawnPollInterval, true, 0.1f);
	}
}

void UBH_StanceRadialComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PollTimer);
	}
	if (BoundLoadout)
	{
		BoundLoadout->OnAvailableStancesChanged.RemoveDynamic(this, &UBH_StanceRadialComponent::HandleStancesChanged);
		BoundLoadout = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

APawn* UBH_StanceRadialComponent::GetControlledPawn() const
{
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	return PC ? PC->GetPawn() : nullptr;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void UBH_StanceRadialComponent::HandleInputBound(UEnhancedInputComponent* InputComponent)
{
	if (InputComponent && HoldOpenAction)
	{
		InputComponent->BindAction(HoldOpenAction, ETriggerEvent::Triggered, this, &UBH_StanceRadialComponent::HandleHoldTriggered);
		InputComponent->BindAction(HoldOpenAction, ETriggerEvent::Completed, this, &UBH_StanceRadialComponent::HandleHoldCompleted);
	}
}

void UBH_StanceRadialComponent::HandleHoldTriggered(const FInputActionValue& Value)
{
	OpenWheel();
}

void UBH_StanceRadialComponent::HandleHoldCompleted(const FInputActionValue& Value)
{
	CloseWheel(/*bConfirm*/ true);
}

void UBH_StanceRadialComponent::OpenWheel()
{
	RefreshConsumableSlots(); // fresh stack counts; a rebuild is only allowed while the wheel is closed
	if (State == ERadialSelectorState::Closed && GetSegmentCount() > 0)
	{
		OpenMenu();
	}
}

void UBH_StanceRadialComponent::CloseWheel(bool bConfirm)
{
	if (State == ERadialSelectorState::Open)
	{
		CloseMenu(/*bInputWasExplicitCancel*/ !bConfirm);
	}
}

void UBH_StanceRadialComponent::DebugHoverSegment(int32 Index)
{
	if (State == ERadialSelectorState::Open && MenuData && MenuData->Segments.IsValidIndex(Index))
	{
		HoveredSegmentIndex = Index;
		OnSegmentHovered.Broadcast(Index);
	}
}

// ---------------------------------------------------------------------------
// Wheel content
// ---------------------------------------------------------------------------

int32 UBH_StanceRadialComponent::GetSegmentCount() const
{
	return MenuData ? MenuData->Segments.Num() : 0;
}

TArray<FName> UBH_StanceRadialComponent::GetSegmentIdentifiers() const
{
	TArray<FName> Out;
	if (MenuData)
	{
		for (const FRadialSelectorSegment& Segment : MenuData->Segments)
		{
			Out.Add(Segment.Identifier);
		}
	}
	return Out;
}

void UBH_StanceRadialComponent::RebuildFromStances(const TArray<FName>& Stances)
{
	// MenuData may only be swapped while the wheel is closed.
	if (State == ERadialSelectorState::Open)
	{
		CloseMenu(/*bInputWasExplicitCancel*/ true, /*bForceFullClose*/ true);
	}

	if (bEightSlotRadial)
	{
		RebuildEightSlot();
		return;
	}

	URadialSelectorMenuData* Data = NewObject<URadialSelectorMenuData>(this, NAME_None, RF_Transient);
	const APawn* Pawn = GetControlledPawn();

	TArray<FName> WheelStances = Stances;
	if (bIncludePlaceholderStancesInWheel)
	{
		for (const FGameplayTag& Placeholder : { TAG_Stance_Weapon_OneHandedSword.GetTag(), TAG_Stance_Weapon_Bow.GetTag(), TAG_Stance_Weapon_Crossbow.GetTag() })
		{
			WheelStances.AddUnique(BH_Stance::ToLegacyName(Placeholder));
		}
	}

	for (const FName& Stance : WheelStances)
	{
		FRadialSelectorSegment Segment;
		Segment.Identifier = Stance;
		const FGameplayTag StanceTag = BH_Stance::FromLegacyName(Stance);
		const FText* Friendly = StanceTag.IsValid() ? StanceDisplayNamesByTag.Find(StanceTag) : nullptr;
		if (!Friendly)
		{
			Friendly = StanceDisplayNames.Find(Stance);
		}
		Segment.DisplayName = Friendly ? *Friendly : FText::FromName(Stance);
		Segment.Icon = UBH_CombatFunctionLibrary::GetStanceIconForPose(Pawn, Stance);
		Data->Segments.Add(Segment);
	}
	URadialSelectorMenuLayout* LayoutToUse = StanceLayout.Get();
	if (!LayoutToUse && MenuData)
	{
		LayoutToUse = MenuData->Layout.Get();
	}
	Data->Layout = LayoutToUse;
	MenuData = Data;
	OnRadialSlotsChanged.Broadcast();
}

// ---------------------------------------------------------------------------
// Eight-slot mode (Phase 8B)
// ---------------------------------------------------------------------------

namespace BH_StanceRadialPrivate
{
	static FName WeaponSlotIdentifier(int32 SlotIndex, bool bEmpty)
	{
		return FName(*FString::Printf(TEXT("%s_%s"), bEmpty ? TEXT("Empty") : TEXT("Weapon"), SlotIndex == 0 ? TEXT("A") : TEXT("B")));
	}

	static FName ConsumableIdentifier(int32 ConsumableIndex)
	{
		return FName(*FString::Printf(TEXT("Consumable_%d"), ConsumableIndex + 1));
	}
}

TArray<FBH_RadialSlotData> UBH_StanceRadialComponent::GetRadialSlots() const
{
	TArray<FBH_RadialSlotData> Slots;
	const APawn* Pawn = GetControlledPawn();
	const UBH_LoadoutComponent* Loadout = UBH_LoadoutComponent::FindLoadoutComponent(Pawn);

	// Phase 12F: set B can be locked per player (Island 1). A locked set reads as an empty, unavailable wedge.
	const APlayerController* OwnerController = Cast<APlayerController>(GetOwner());
	const ABH_PlayerState* OwnerBHState = OwnerController ? OwnerController->GetPlayerState<ABH_PlayerState>() : nullptr;
	const bool bSetBLocked = OwnerBHState && !OwnerBHState->IsWeaponSetBUnlocked();

	for (int32 SetIndex = 0; SetIndex < 2; ++SetIndex)
	{
		const bool bLockedSet = (SetIndex == 1) && bSetBLocked;
		FBH_RadialSlotData Slot;
		Slot.SlotIndex = SetIndex;
		Slot.Kind = EBH_RadialSlotKind::WeaponSet;
		Slot.StanceName = (Loadout && !bLockedSet) ? Loadout->GetStanceForSet(SetIndex == 0 ? EBH_LoadoutSet::A : EBH_LoadoutSet::B) : NAME_None;
		Slot.bEmpty = Slot.StanceName.IsNone();
		Slot.bAvailable = !Slot.bEmpty;
		Slot.Identifier = BH_StanceRadialPrivate::WeaponSlotIdentifier(SetIndex, Slot.bEmpty);
		if (Slot.bEmpty)
		{
			Slot.DisplayName = SetIndex == 0 ? NSLOCTEXT("BHStance", "EmptySetA", "Set A (empty)")
				: (bLockedSet ? NSLOCTEXT("BHStance", "LockedSetB", "Set B (locked)") : NSLOCTEXT("BHStance", "EmptySetB", "Set B (empty)"));
		}
		else
		{
			const FGameplayTag StanceTag = BH_Stance::FromLegacyName(Slot.StanceName);
			const FText* Friendly = StanceTag.IsValid() ? StanceDisplayNamesByTag.Find(StanceTag) : nullptr;
			if (!Friendly)
			{
				Friendly = StanceDisplayNames.Find(Slot.StanceName);
			}
			Slot.DisplayName = Friendly ? *Friendly : FText::FromName(Slot.StanceName);
			Slot.Icon = UBH_CombatFunctionLibrary::GetStanceIconForPose(Pawn, Slot.StanceName);
		}
		Slots.Add(Slot);
	}

	for (int32 ConsumableIndex = 0; ConsumableIndex < NumConsumableSlots; ++ConsumableIndex)
	{
		FBH_RadialSlotData Slot;
		Slot.SlotIndex = 2 + ConsumableIndex;
		Slot.Kind = EBH_RadialSlotKind::Consumable;
		Slot.Identifier = BH_StanceRadialPrivate::ConsumableIdentifier(ConsumableIndex);
		if (ConsumableSlots.IsValidIndex(ConsumableIndex) && !ConsumableSlots[ConsumableIndex].IsEmpty())
		{
			const FBH_RadialConsumableSlot& Source = ConsumableSlots[ConsumableIndex];
			Slot.bEmpty = false;
			Slot.DisplayName = Source.DisplayName;
			Slot.Icon = Source.Icon;
			Slot.Quantity = Source.Quantity;
			// A filled slot at count 0 keeps its place but is not usable (refreshed from the inventory every time the wheel opens).
			Slot.bAvailable = Source.Quantity >= 1;
		}
		else
		{
			Slot.DisplayName = NSLOCTEXT("BHStance", "EmptyConsumable", "Empty");
		}
		Slots.Add(Slot);
	}
	return Slots;
}

int32 UBH_StanceRadialComponent::GetLiveConsumableCount(int32 ConsumableIndex) const
{
	if (!ConsumableSlots.IsValidIndex(ConsumableIndex) || ConsumableSlots[ConsumableIndex].IsEmpty())
	{
		return 0;
	}
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	const UClass* ItemClass = Cast<UClass>(ConsumableSlots[ConsumableIndex].ItemReference.LoadSynchronous());
	if (!PC || !ItemClass || !ItemClass->IsChildOf(UNarrativeItem::StaticClass()))
	{
		return 0;
	}
	return UBH_LootLibrary::GetItemCount(PC->GetPlayerState<APlayerState>(), const_cast<UClass*>(ItemClass));
}

bool UBH_StanceRadialComponent::IsConsumableSlotAvailable(int32 RadialSlotIndex) const
{
	return GetLiveConsumableCount(RadialSlotIndex - 2) >= 1;
}

void UBH_StanceRadialComponent::RebuildEightSlot()
{
	URadialSelectorMenuData* Data = NewObject<URadialSelectorMenuData>(this, NAME_None, RF_Transient);
	WeaponSlotStances.Reset();

	for (const FBH_RadialSlotData& Slot : GetRadialSlots())
	{
		FRadialSelectorSegment Segment;
		Segment.Identifier = Slot.Identifier;
		Segment.DisplayName = Slot.DisplayName;
		Segment.Icon = Slot.Icon;
		if (Slot.bEmpty)
		{
			// Dim empty wedges; they stay in the ring so the other slots keep their angular positions.
			Segment.bOverrideWedgeColor = true;
			Segment.WedgeColorOverride = FLinearColor(0.25f, 0.25f, 0.25f, 0.6f);
		}
		else if (!Slot.bAvailable)
		{
			// Phase 11E: a consumable at count 0 keeps its place and its name but is greyed (and selecting it is a no-op, see UseConsumableSlot).
			Segment.bOverrideWedgeColor = true;
			Segment.WedgeColorOverride = FLinearColor(0.12f, 0.12f, 0.12f, 0.55f);
		}
		if (Slot.Kind == EBH_RadialSlotKind::WeaponSet)
		{
			WeaponSlotStances.Add(Slot.StanceName);
		}
		Data->Segments.Add(Segment);
	}

	URadialSelectorMenuLayout* LayoutToUse = StanceLayout.Get();
	if (!LayoutToUse && MenuData)
	{
		LayoutToUse = MenuData->Layout.Get();
	}
	Data->Layout = LayoutToUse;
	MenuData = Data;
	OnRadialSlotsChanged.Broadcast();
}

bool UBH_StanceRadialComponent::SetConsumableSlot(int32 ConsumableIndex, const FBH_RadialConsumableSlot& NewSlot)
{
	if (ConsumableIndex < 0 || ConsumableIndex >= NumConsumableSlots)
	{
		return false;
	}
	ConsumableSlots.SetNum(NumConsumableSlots);
	ConsumableSlots[ConsumableIndex] = NewSlot;
	if (bEightSlotRadial)
	{
		RebuildFromStances(BoundLoadout ? BoundLoadout->AvailableStances : TArray<FName>());
	}
	else
	{
		OnRadialSlotsChanged.Broadcast();
	}
	return true;
}

bool UBH_StanceRadialComponent::ClearConsumableSlot(int32 ConsumableIndex)
{
	return SetConsumableSlot(ConsumableIndex, FBH_RadialConsumableSlot());
}

bool UBH_StanceRadialComponent::UseConsumableSlot(int32 RadialSlotIndex)
{
	const int32 ConsumableIndex = RadialSlotIndex - 2;
	if (ConsumableIndex < 0 || ConsumableIndex >= NumConsumableSlots || !ConsumableSlots.IsValidIndex(ConsumableIndex))
	{
		return false;
	}

	const FBH_RadialConsumableSlot& Slot = ConsumableSlots[ConsumableIndex];
	if (Slot.IsEmpty())
	{
		return false; // empty slot: nothing to do
	}

	// Phase 11E: a slot at count 0 is disabled. Choosing it does nothing: no event, no request, no RPC. (Live count, not the cached label.)
	if (GetLiveConsumableCount(ConsumableIndex) < 1)
	{
		UE_LOG(LogBHCombat, Verbose, TEXT("Radial consumable slot %d chosen but the stack is empty: ignored."), ConsumableIndex + 1);
		return false;
	}

	OnConsumableSlotUsed.Broadcast(ConsumableIndex, Slot);

	// Phase 11D: the slot names a Narrative item class; the server validates and runs UBH_GA_UseConsumable on the pawn's ASC.
	UClass* ItemClass = Cast<UClass>(Slot.ItemReference.LoadSynchronous());
	if (!ItemClass || !ItemClass->IsChildOf(UNarrativeItem::StaticClass()))
	{
		UE_LOG(LogBHCombat, Warning, TEXT("Radial consumable slot %d (%s) is not a Narrative item class."), ConsumableIndex + 1, *Slot.ItemReference.ToString());
		return false;
	}
	const bool bSent = UBH_ConsumableLibrary::RequestUseConsumable(GetControlledPawn(), ItemClass);
	UE_LOG(LogBHCombat, Log, TEXT("Radial consumable slot %d chosen (%s): request %s."), ConsumableIndex + 1, *GetNameSafe(ItemClass), bSent ? TEXT("sent") : TEXT("not sent"));
	return bSent;
}

bool UBH_StanceRadialComponent::RefreshConsumableSlots()
{
	if (!bEightSlotRadial || State == ERadialSelectorState::Open)
	{
		return false;
	}
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (!PC || !PC->IsLocalController())
	{
		return false;
	}
	ConsumableSlots.SetNum(NumConsumableSlots);

	// First refresh: fill the default slots, but only when nobody configured any.
	if (bAutoPopulateConsumables && !bDefaultConsumablesApplied)
	{
		bool bAllEmpty = true;
		for (const FBH_RadialConsumableSlot& Candidate : ConsumableSlots)
		{
			bAllEmpty = bAllEmpty && Candidate.IsEmpty();
		}
		if (bAllEmpty)
		{
			ConsumableSlots[0].ItemReference = TSoftObjectPtr<UObject>(FSoftObjectPath(BH_LootPaths::HeartwoodSap));
			ConsumableSlots[1].ItemReference = TSoftObjectPtr<UObject>(FSoftObjectPath(BH_LootPaths::WardensIncense));
		}
	}
	bDefaultConsumablesApplied = true;

	const APlayerState* LocalPlayerState = PC->GetPlayerState<APlayerState>();
	bool bChanged = false;
	for (FBH_RadialConsumableSlot& Slot : ConsumableSlots)
	{
		if (Slot.IsEmpty())
		{
			continue;
		}
		UClass* ItemClass = Cast<UClass>(Slot.ItemReference.LoadSynchronous());
		if (!ItemClass || !ItemClass->IsChildOf(UNarrativeItem::StaticClass()))
		{
			continue;
		}
		const int32 Count = UBH_LootLibrary::GetItemCount(LocalPlayerState, ItemClass);
		// Count 0: the slot stays, the "x0" is dropped (the wedge is greyed instead, see RebuildEightSlot).
		const FText ItemName = UBH_LootLibrary::GetItemDisplayName(ItemClass);
		const FText Label = Count >= 1
			? FText::Format(NSLOCTEXT("BHStance", "ConsumableLabel", "{0} x{1}"), ItemName, FText::AsNumber(Count))
			: ItemName;
		if (Slot.Quantity != Count || !Slot.DisplayName.EqualTo(Label))
		{
			Slot.Quantity = Count;
			Slot.DisplayName = Label;
			bChanged = true;
		}
		if (Slot.Icon.IsNull())
		{
			if (const UNarrativeItem* ItemDefaults = ItemClass->GetDefaultObject<UNarrativeItem>())
			{
				Slot.Icon = ItemDefaults->Thumbnail;
				bChanged = bChanged || !Slot.Icon.IsNull();
			}
		}
	}

	if (bChanged)
	{
		RebuildFromStances(BoundLoadout ? BoundLoadout->AvailableStances : TArray<FName>());
	}
	return bChanged;
}

// ---------------------------------------------------------------------------
// Stance switching
// ---------------------------------------------------------------------------

bool UBH_StanceRadialComponent::SelectStance(FName Stance)
{
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	APawn* Pawn = GetControlledPawn();
	if (!PC || !Pawn || Stance.IsNone())
	{
		return false;
	}

	const FGameplayTag StanceTag = BH_Stance::FromLegacyName(Stance);
	if (BH_Stance::IsPlaceholderStance(StanceTag))
	{
		// Phase 8D: keep the current stance (and the PC's MeleeAttackAbilityClass) untouched.
		UBH_StanceComponent::NotifyStanceNotImplemented(Pawn, StanceTag);
		return false;
	}

	// Phase 12F: a stance that only set B provides is refused while set B is locked for this player.
	const ABH_PlayerState* BHPlayerState = PC->GetPlayerState<ABH_PlayerState>();
	if (BHPlayerState && !BHPlayerState->IsWeaponSetBUnlocked())
	{
		const UBH_LoadoutComponent* PawnLoadout = UBH_LoadoutComponent::FindLoadoutComponent(Pawn);
		if (PawnLoadout && PawnLoadout->GetStanceForSet(EBH_LoadoutSet::A) != Stance && PawnLoadout->GetStanceForSet(EBH_LoadoutSet::B) == Stance)
		{
			return false;
		}
	}

	const bool bRequested = UBH_CombatFunctionLibrary::RequestStanceByName(Pawn, Stance.ToString());

	// Same fallback chain the Blueprint CycleStance had: data-driven ability, else the Dual/SnS class variables.
	TSubclassOf<UGameplayAbility> AbilityClass = UBH_CombatFunctionLibrary::GetMeleeAbilityForPose(Pawn, Stance);
	if (!AbilityClass)
	{
		const FName FallbackVar = (Stance == FName(TEXT("DualSword"))) ? FName(TEXT("DualSwordAbilityClass")) : FName(TEXT("SwordShieldAbilityClass"));
		if (const FClassProperty* Fallback = CastField<FClassProperty>(PC->GetClass()->FindPropertyByName(FallbackVar)))
		{
			AbilityClass = Cast<UClass>(Fallback->GetObjectPropertyValue_InContainer(PC));
		}
	}
	if (AbilityClass)
	{
		if (const FClassProperty* Target = CastField<FClassProperty>(PC->GetClass()->FindPropertyByName(FName(TEXT("MeleeAttackAbilityClass")))))
		{
			Target->SetObjectPropertyValue_InContainer(PC, AbilityClass.Get());
		}
	}
	return bRequested;
}

void UBH_StanceRadialComponent::CycleStance()
{
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	APawn* Pawn = GetControlledPawn();
	if (!PC || !Pawn)
	{
		return;
	}

	TArray<FString> Cycle;
	const UBH_LoadoutComponent* Loadout = UBH_LoadoutComponent::FindLoadoutComponent(Pawn);
	if (Loadout)
	{
		// A loadout exists: its list is authoritative. An empty list (e.g. items not replicated yet) means do nothing,
		// never fall back to the legacy cycle (the server would reject stances the pawn does not own).
		for (const FName& Stance : Loadout->AvailableStances)
		{
			Cycle.Add(Stance.ToString());
		}
	}
	else
	{
		// No loadout component at all: keep the legacy fixed cycle on the controller.
		if (const FArrayProperty* ArrayProp = CastField<FArrayProperty>(PC->GetClass()->FindPropertyByName(FName(TEXT("StanceCycle")))))
		{
			if (CastField<FStrProperty>(ArrayProp->Inner))
			{
				Cycle = *ArrayProp->ContainerPtrToValuePtr<TArray<FString>>(PC);
			}
		}
	}
	if (Cycle.IsEmpty())
	{
		return;
	}

	const int32 Next = UBH_CombatFunctionLibrary::GetNextStanceIndex(Pawn, Cycle);
	if (Cycle.IsValidIndex(Next))
	{
		SelectStance(FName(*Cycle[Next]));
	}
}

void UBH_StanceRadialComponent::HandleSegmentSelected(const FRadialSelectorSegment& SelectedSegment)
{
	if (bEightSlotRadial && MenuData)
	{
		const int32 SlotIndex = MenuData->Segments.IndexOfByPredicate([&SelectedSegment](const FRadialSelectorSegment& Candidate)
		{
			return Candidate.Identifier == SelectedSegment.Identifier;
		});
		if (SlotIndex == INDEX_NONE)
		{
			return;
		}
		if (SlotIndex < 2)
		{
			if (WeaponSlotStances.IsValidIndex(SlotIndex) && !WeaponSlotStances[SlotIndex].IsNone())
			{
				SelectStance(WeaponSlotStances[SlotIndex]);
			}
			return; // empty weapon set: nothing to select
		}
		UseConsumableSlot(SlotIndex); // ignores a slot at count 0
		return;
	}
	SelectStance(SelectedSegment.Identifier);
}

// ---------------------------------------------------------------------------
// Loadout binding
// ---------------------------------------------------------------------------

void UBH_StanceRadialComponent::PollPawn()
{
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (!PC || !PC->IsLocalController())
	{
		return;
	}

	RefreshConsumableSlots(); // Phase 11D: stack counts follow the inventory while the wheel is closed

	UBH_LoadoutComponent* Loadout = UBH_LoadoutComponent::FindLoadoutComponent(PC->GetPawn());
	if (Loadout == BoundLoadout)
	{
		return;
	}

	if (BoundLoadout)
	{
		BoundLoadout->OnAvailableStancesChanged.RemoveDynamic(this, &UBH_StanceRadialComponent::HandleStancesChanged);
	}
	BoundLoadout = Loadout;
	if (BoundLoadout)
	{
		BoundLoadout->OnAvailableStancesChanged.AddUniqueDynamic(this, &UBH_StanceRadialComponent::HandleStancesChanged);
		HandleStancesChanged(BoundLoadout->AvailableStances);
	}
}

void UBH_StanceRadialComponent::HandleStancesChanged(const TArray<FName>& Stances)
{
	RebuildFromStances(Stances);

	// Current stance no longer available -> first available one.
	const APawn* Pawn = GetControlledPawn();
	const FString Pose = UBH_CombatFunctionLibrary::GetCurrentOverlayPoseDisplayName(Pawn);
	if (Stances.Num() > 0 && !Pose.IsEmpty() && !Stances.Contains(FName(*Pose)))
	{
		SelectStance(Stances[0]);
	}
}
