// Blackwood Hollow - player state holding the Narrative inventory (implementation)

#include "Player/BH_PlayerState.h"
#include "InventoryComponent.h"
#include "AbilitySystem/Abilities/AH_GA_FragmentBase.h"
#include "AbilitySystem/Abilities/AH_GA_OverloadBurst.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "Components/BPC_HeartFragment.h"
#include "Progression/BH_ProgressionComponent.h"
#include "AI/BH_EnemyWaveSpawner.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "Components/ActorComponent.h"
#include "UObject/UnrealType.h"

ABH_PlayerState::ABH_PlayerState()
{
	Inventory = CreateDefaultSubobject<UNarrativeInventoryComponent>(TEXT("Inventory"));
	Inventory->SetIsReplicated(true);
	// Narrative Inventory replicates its item UObjects through the legacy virtual ReplicateSubobjects(). The project
	// enables net.SubObjects.DefaultUseSubObjectReplicationList=1 (GASP template), which would skip that path and
	// leave every item null on clients, so opt this component back into the legacy path.
	// (The flag is protected on UActorComponent with no setter, so it is written through reflection.)
	if (const FBoolProperty* Flag = CastField<FBoolProperty>(UActorComponent::StaticClass()->FindPropertyByName(TEXT("bReplicateUsingRegisteredSubObjectList"))))
	{
		Flag->SetPropertyValue_InContainer(Inventory, false);
	}

	// Phase 9: XP / level (the component is replicated by default).
	Progression = CreateDefaultSubobject<UBH_ProgressionComponent>(TEXT("Progression"));

	// PlayerState defaults to ~1 Hz; equipment changes should reach the client quickly (we also ForceNetUpdate on changes).
	SetNetUpdateFrequency(10.f);

	// Phase 8B: three category-restricted Heart-Fragment slots. Overload Burst (Offensive) defaults to slot 1.
	FragmentSlotCategories = { TAG_Fragment_Category_Offensive.GetTag(), TAG_Fragment_Category_Vitality.GetTag(), TAG_Fragment_Category_BlightResist.GetTag() };
	FragmentSlots.SetNum(NumFragmentSlots);
	FragmentSlots[0] = TSubclassOf<UAH_GA_FragmentBase>(UAH_GA_OverloadBurst::StaticClass());
}

void ABH_PlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABH_PlayerState, FragmentSlots);
}

// ============================================================================
// Heart-Fragment slots (Phase 8B)
// ============================================================================

FGameplayTag ABH_PlayerState::GetSlotCategory(int32 Slot) const
{
	return FragmentSlotCategories.IsValidIndex(Slot) ? FragmentSlotCategories[Slot] : FGameplayTag();
}

TSubclassOf<UAH_GA_FragmentBase> ABH_PlayerState::GetFragmentInSlot(int32 Slot) const
{
	return (Slot >= 0 && Slot < NumFragmentSlots && FragmentSlots.IsValidIndex(Slot)) ? FragmentSlots[Slot] : nullptr;
}

bool ABH_PlayerState::IsFragmentSlotEmpty(int32 Slot) const
{
	return GetFragmentInSlot(Slot) == nullptr;
}

FGameplayTag ABH_PlayerState::GetFragmentCategory(TSubclassOf<UAH_GA_FragmentBase> FragmentClass)
{
	const UAH_GA_FragmentBase* CDO = FragmentClass ? FragmentClass->GetDefaultObject<UAH_GA_FragmentBase>() : nullptr;
	return CDO ? CDO->FragmentCategory : FGameplayTag();
}

bool ABH_PlayerState::CanEquipInSlot(int32 Slot, TSubclassOf<UAH_GA_FragmentBase> FragmentClass) const
{
	if (Slot < 0 || Slot >= NumFragmentSlots || !FragmentClass)
	{
		return false;
	}

	const FGameplayTag FragmentCategory = GetFragmentCategory(FragmentClass);
	const FGameplayTag SlotCategory = GetSlotCategory(Slot);
	if (!FragmentCategory.IsValid() || !SlotCategory.IsValid() || !FragmentCategory.MatchesTag(SlotCategory))
	{
		return false;
	}

	// One copy per loadout: the same fragment cannot sit in two slots (they would share one ability spec).
	for (int32 OtherSlot = 0; OtherSlot < NumFragmentSlots; ++OtherSlot)
	{
		if (OtherSlot != Slot && GetFragmentInSlot(OtherSlot) == FragmentClass)
		{
			return false;
		}
	}
	return true;
}

bool ABH_PlayerState::EquipFragment(int32 Slot, TSubclassOf<UAH_GA_FragmentBase> FragmentClass)
{
	if (!CanEquipInSlot(Slot, FragmentClass))
	{
		return false;
	}
	if (HasAuthority())
	{
		return ApplyFragmentSlot(Slot, FragmentClass);
	}
	ServerEquipFragment(Slot, FragmentClass);
	return true;
}

bool ABH_PlayerState::UnequipFragment(int32 Slot)
{
	if (Slot < 0 || Slot >= NumFragmentSlots)
	{
		return false;
	}
	if (HasAuthority())
	{
		return ApplyFragmentSlot(Slot, nullptr);
	}
	ServerEquipFragment(Slot, nullptr);
	return true;
}

bool ABH_PlayerState::ActivateFragmentSlot(int32 Slot)
{
	const APawn* OwnedPawn = GetPawn();
	UBPC_HeartFragment* HeartFragment = OwnedPawn ? OwnedPawn->FindComponentByClass<UBPC_HeartFragment>() : nullptr;
	return HeartFragment ? HeartFragment->TryActivateFragment(Slot) : false;
}

bool ABH_PlayerState::ServerEquipFragment_Validate(int32 Slot, TSubclassOf<UAH_GA_FragmentBase> FragmentClass)
{
	return Slot >= 0 && Slot < NumFragmentSlots;
}

void ABH_PlayerState::ServerEquipFragment_Implementation(int32 Slot, TSubclassOf<UAH_GA_FragmentBase> FragmentClass)
{
	ApplyFragmentSlot(Slot, FragmentClass);
}

bool ABH_PlayerState::ApplyFragmentSlot(int32 Slot, TSubclassOf<UAH_GA_FragmentBase> FragmentClass)
{
	if (!HasAuthority() || Slot < 0 || Slot >= NumFragmentSlots)
	{
		return false;
	}
	if (FragmentClass && !CanEquipInSlot(Slot, FragmentClass))
	{
		UE_LOG(LogBHCombat, Warning, TEXT("Fragment '%s' refused for slot %d on '%s' (category mismatch or already equipped)."),
			*GetNameSafe(FragmentClass), Slot + 1, *GetNameSafe(this));
		return false;
	}

	FragmentSlots.SetNum(NumFragmentSlots);
	if (FragmentSlots[Slot] == FragmentClass)
	{
		return true;
	}
	FragmentSlots[Slot] = FragmentClass;
	ForceNetUpdate();

	// Mirror onto the pawn (grants / clears the ability) and refresh the listen-server host's own UI.
	if (const APawn* OwnedPawn = GetPawn())
	{
		if (UBPC_HeartFragment* HeartFragment = OwnedPawn->FindComponentByClass<UBPC_HeartFragment>())
		{
			HeartFragment->SyncFromPlayerState();
		}
	}
	OnFragmentSlotsChanged.Broadcast(Slot);
	return true;
}

void ABH_PlayerState::OnRep_FragmentSlots()
{
	OnFragmentSlotsChanged.Broadcast(-1);
}

// ============================================================================
// Debug RPCs (non-shipping): server side of the bh.* console commands typed in a client window
// ============================================================================

#if !UE_BUILD_SHIPPING

bool ABH_PlayerState::ServerDebugGrantXP_Validate(int32 Amount)
{
	return Amount >= 0 && Amount <= MaxDebugXP;
}

void ABH_PlayerState::ServerDebugGrantXP_Implementation(int32 Amount)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (UBH_ProgressionComponent* PlayerProgression = UBH_ProgressionComponent::FindProgression(It->Get()))
		{
			PlayerProgression->AddXP(Amount);
			UE_LOG(LogBHCombat, Log, TEXT("bh.XP.Grant (remote, from '%s'): +%d XP -> level %d, %d / %d XP."), *GetPlayerName(), Amount,
				PlayerProgression->GetLevel(), PlayerProgression->GetCurrentXP(), PlayerProgression->GetXPToNextLevel());
		}
	}
}

bool ABH_PlayerState::ServerDebugSetLevel_Validate(int32 NewLevel)
{
	return NewLevel >= 1 && NewLevel <= MaxDebugLevel;
}

void ABH_PlayerState::ServerDebugSetLevel_Implementation(int32 NewLevel)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (UBH_ProgressionComponent* PlayerProgression = UBH_ProgressionComponent::FindProgression(It->Get()))
		{
			PlayerProgression->SetLevel(NewLevel);
			UE_LOG(LogBHCombat, Log, TEXT("bh.Level.Set (remote, from '%s'): level %d."), *GetPlayerName(), PlayerProgression->GetLevel());
		}
	}
}

bool ABH_PlayerState::ServerDebugSkipToWave_Validate(int32 WaveNumber)
{
	return WaveNumber >= 1 && WaveNumber <= MaxDebugWave;
}

void ABH_PlayerState::ServerDebugSkipToWave_Implementation(int32 WaveNumber)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (TActorIterator<ABH_EnemyWaveSpawner> It(World); It; ++It)
	{
		It->DebugSkipToWave(WaveNumber);
	}
}

bool ABH_PlayerState::ServerDebugSpawnBoss_Validate()
{
	return true;
}

void ABH_PlayerState::ServerDebugSpawnBoss_Implementation()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	int32 Count = 0;
	for (TActorIterator<ABH_EnemyWaveSpawner> It(World); It; ++It)
	{
		It->DebugSpawnBoss();
		++Count;
	}
	UE_LOG(LogBHCombat, Log, TEXT("bh.Arena.SpawnBoss (remote, from '%s'): %d wave spawner(s)."), *GetPlayerName(), Count);
}

#else // UE_BUILD_SHIPPING: stubs so the RPCs link; validation rejects every call.
bool ABH_PlayerState::ServerDebugGrantXP_Validate(int32) { return false; }
void ABH_PlayerState::ServerDebugGrantXP_Implementation(int32) {}
bool ABH_PlayerState::ServerDebugSetLevel_Validate(int32) { return false; }
void ABH_PlayerState::ServerDebugSetLevel_Implementation(int32) {}
bool ABH_PlayerState::ServerDebugSkipToWave_Validate(int32) { return false; }
void ABH_PlayerState::ServerDebugSkipToWave_Implementation(int32) {}
bool ABH_PlayerState::ServerDebugSpawnBoss_Validate() { return false; }
void ABH_PlayerState::ServerDebugSpawnBoss_Implementation() {}
#endif // !UE_BUILD_SHIPPING
