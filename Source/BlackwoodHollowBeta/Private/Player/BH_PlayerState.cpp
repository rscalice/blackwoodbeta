// Blackwood Hollow - player state holding the Narrative inventory (implementation)

#include "Player/BH_PlayerState.h"
#include "Hints/BH_HintLibrary.h"
#include "Hints/BH_PlayerHintComponent.h"
#include "Player/BH_PlayerMenuComponent.h"
#include "InventoryComponent.h"
#include "AbilitySystem/Abilities/AH_GA_FragmentBase.h"
#include "AbilitySystem/Abilities/AH_GA_OverloadBurst.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystem/Effects/AH_GE_CombatEffects.h"
#include "Components/BPC_HeartFragment.h"
#include "Consumables/BH_ConsumableLibrary.h"
#include "Progression/BH_ProgressionComponent.h"
#include "AI/BH_EnemyWaveSpawner.h"
#include "Characters/BH_EnemyBase.h"
#include "Combat/BH_EnemyRank.h"
#include "Progression/BH_RPGSettings.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "NavigationSystem.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "Player/BH_GameState.h"
#include "Player/BH_PartyLibrary.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "AbilitySystemGlobals.h"
#include "AbilitySystem/StatusEffects/BH_FractureEffects.h"
#include "TimerManager.h"
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
	HintComponent = CreateDefaultSubobject<UBH_PlayerHintComponent>(TEXT("HintComponent"));
	PlayerMenu = CreateDefaultSubobject<UBH_PlayerMenuComponent>(TEXT("PlayerMenu"));

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
	DOREPLIFETIME(ABH_PlayerState, bWeaponSetBUnlocked);
	DOREPLIFETIME(ABH_PlayerState, bOverloadBurstLearned);
	DOREPLIFETIME(ABH_PlayerState, AttunedHubs);
	DOREPLIFETIME(ABH_PlayerState, bStarterPointsRetired);
	DOREPLIFETIME(ABH_PlayerState, PlayerSlot);
	DOREPLIFETIME(ABH_PlayerState, DeathCause);
	DOREPLIFETIME(ABH_PlayerState, PartyStatus);
}

void ABH_PlayerState::BeginPlay()
{
	Super::BeginPlay();

	// 13I-1: the server samples the partner-panel summary at most 5 times per second, and only when something marked it dirty.
	if (HasAuthority())
	{
		if (Progression)
		{
			Progression->OnLevelUp.AddDynamic(this, &ABH_PlayerState::HandleStatusLevelUp);
		}
		bPartyStatusDirty = true;
		if (const UWorld* StateWorld = GetWorld())
		{
			StateWorld->GetTimerManager().SetTimer(PartyStatusTimer, this, &ABH_PlayerState::TickPartyStatus, PartyStatusInterval, true);
		}
	}

	// Server only: hand out the lowest free party slot (1..MaxPlayerSlots) among the players currently in the game.
	if (HasAuthority() && PlayerSlot == 0)
	{
		TSet<int32> Taken;
		if (const AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr)
		{
			for (const APlayerState* Other : GS->PlayerArray)
			{
				if (const ABH_PlayerState* BHOther = Cast<ABH_PlayerState>(Other))
				{
					if (BHOther != this && BHOther->PlayerSlot > 0)
					{
						Taken.Add(BHOther->PlayerSlot);
					}
				}
			}
		}
		int32 Slot = 1;
		while (Slot < MaxPlayerSlots && Taken.Contains(Slot))
		{
			++Slot;
		}
		PlayerSlot = Slot;
		ForceNetUpdate();
	}
}

// ============================================================================
// Progression gates (Phase 12F)
// ============================================================================

void ABH_PlayerState::SetWeaponSetBUnlocked(bool bUnlocked)
{
	if (!HasAuthority() || bWeaponSetBUnlocked == bUnlocked)
	{
		return;
	}
	bWeaponSetBUnlocked = bUnlocked;
	ForceNetUpdate();
	OnRep_ProgressionGates(); // the server does not get the RepNotify
}

bool ABH_PlayerState::GrantOverloadBurst()
{
	if (!HasAuthority())
	{
		return false;
	}

	const TSubclassOf<UAH_GA_FragmentBase> BurstClass = UAH_GA_OverloadBurst::StaticClass();
	bool bEquipped = false;
	for (int32 SlotIndex = 0; SlotIndex < NumFragmentSlots; ++SlotIndex)
	{
		if (GetFragmentInSlot(SlotIndex) == BurstClass)
		{
			bEquipped = true; // already in a slot
			break;
		}
	}
	for (int32 SlotIndex = 0; SlotIndex < NumFragmentSlots && !bEquipped; ++SlotIndex)
	{
		if (CanEquipInSlot(SlotIndex, BurstClass))
		{
			bEquipped = EquipFragment(SlotIndex, BurstClass);
		}
	}

	if (!bOverloadBurstLearned)
	{
		bOverloadBurstLearned = true;
		UBH_HintLibrary::TriggerHintKey(this, TEXT("Skill.OverloadBurst"));
		ForceNetUpdate();
		OnRep_ProgressionGates();
	}
	return bEquipped;
}

bool ABH_PlayerState::AttuneHub(FName HubName, bool bRetireStarterPoints)
{
	if (!HasAuthority() || HubName.IsNone())
	{
		return false;
	}
	bool bChanged = false;
	if (!AttunedHubs.Contains(HubName))
	{
		AttunedHubs.Add(HubName);
		bChanged = true;
	}
	if (bRetireStarterPoints && !bStarterPointsRetired)
	{
		bStarterPointsRetired = true;
		bChanged = true;
	}
	if (bChanged)
	{
		ForceNetUpdate();
		OnRep_ProgressionGates();
		UE_LOG(LogBHCombat, Log, TEXT("%s attuned respawn hub '%s' (starter points retired: %s)."), *GetPlayerName(), *HubName.ToString(), bStarterPointsRetired ? TEXT("yes") : TEXT("no"));
	}
	return bChanged;
}

void ABH_PlayerState::OnRep_ProgressionGates()
{
	OnProgressionGatesChanged.Broadcast();
}

// ============================================================================
// Cause of death (13C)
// ============================================================================

void ABH_PlayerState::RecordDeathCause(const AActor* Killer, bool bBlight)
{
	if (!HasAuthority())
	{
		return;
	}
	FBH_DeathCause Cause;
	Cause.Epoch = static_cast<uint8>(DeathCause.Epoch + 1);
	if (!bBlight && Killer && UBH_EnemyRankLibrary::HasRankData(Killer))
	{
		Cause.bKilledByEnemy = true;
		Cause.Rank = UBH_EnemyRankLibrary::GetRank(Killer);
		Cause.KillerName = UBH_EnemyRankLibrary::GetBossBarName(Killer).ToString();
	}
	DeathCause = Cause;
	ForceNetUpdate();
}

// ============================================================================
// Consumable refusal feedback (Phase 11E)
// ============================================================================

void ABH_PlayerState::NotifyConsumableUseRefused(TSubclassOf<UNarrativeItem> ItemClass, EBH_ConsumableRefusal Reason)
{
	const APlayerController* OwningController = GetPlayerController();
	if (!HasAuthority() || (OwningController && OwningController->IsLocalController()))
	{
		// A client (it only gets here from its own local pre-check), or the listen-server host's own state.
		PresentConsumableRefusal(ItemClass, Reason);
		return;
	}
	// The server refusing on behalf of a remote client: tell that client.
	ClientConsumableUseRefused(ItemClass, Reason);
}

void ABH_PlayerState::ClientConsumableUseRefused_Implementation(TSubclassOf<UNarrativeItem> ItemClass, EBH_ConsumableRefusal Reason)
{
	PresentConsumableRefusal(ItemClass, Reason);
}

void ABH_PlayerState::PresentConsumableRefusal(TSubclassOf<UNarrativeItem> ItemClass, EBH_ConsumableRefusal Reason)
{
#if !UE_BUILD_SHIPPING
	// Works with no widget: a short on-screen line for the local player (a UMG toast bound to OnConsumableUseRefused replaces it).
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(static_cast<uint64>(0xB11C0150), 2.f, FColor::Orange, UBH_ConsumableLibrary::GetRefusalText(Reason).ToString());
	}
#endif
	OnConsumableUseRefused.Broadcast(ItemClass, Reason);
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

bool ABH_PlayerState::ServerDebugUnlockSetB_Validate()
{
	return true;
}

void ABH_PlayerState::ServerDebugUnlockSetB_Implementation()
{
	SetWeaponSetBUnlocked(true);
	UE_LOG(LogBHCombat, Log, TEXT("bh.Loadout.UnlockSetB: weapon set B unlocked for %s."), *GetPlayerName());
}

bool ABH_PlayerState::ServerDebugGrantBurst_Validate()
{
	return true;
}

void ABH_PlayerState::ServerDebugGrantBurst_Implementation()
{
	const bool bEquipped = GrantOverloadBurst();
	UE_LOG(LogBHCombat, Log, TEXT("bh.Skills.GrantBurst: Overload Burst granted to %s (equipped: %s)."), *GetPlayerName(), bEquipped ? TEXT("yes") : TEXT("no"));
}

bool ABH_PlayerState::ServerDebugOpenSpanGate_Validate()
{
	return true;
}

void ABH_PlayerState::ServerDebugOpenSpanGate_Implementation()
{
	if (ABH_GameState* State = GetWorld() ? GetWorld()->GetGameState<ABH_GameState>() : nullptr)
	{
		State->SetWorldFlag(ABH_GameState::SpanGateOpenFlag(), true);
		UE_LOG(LogBHCombat, Log, TEXT("bh.World.OpenSpanGate: world flag '%s' set."), *ABH_GameState::SpanGateOpenFlag().ToString());
	}
	else
	{
		UE_LOG(LogBHCombat, Warning, TEXT("bh.World.OpenSpanGate: the game state is not an ABH_GameState, no world flag store."));
	}
}

bool ABH_PlayerState::ServerDebugSpawnEnemy_Validate(const FString& ShortName, int32 RankOverride, int32 LevelOverride)
{
	return ShortName.Len() > 0 && ShortName.Len() <= 32 && RankOverride >= -1 && RankOverride <= 3 && LevelOverride >= 0 && LevelOverride <= MaxDebugLevel;
}

void ABH_PlayerState::ServerDebugSpawnEnemy_Implementation(const FString& ShortName, int32 RankOverride, int32 LevelOverride)
{
	UWorld* World = GetWorld();
	const APawn* PlayerPawn = GetPawn();
	const UBH_RPGSettings* Settings = UBH_RPGSettings::Get();
	if (!World || !PlayerPawn || !Settings)
	{
		UE_LOG(LogBHCombat, Warning, TEXT("bh.Enemy.Spawn: refused, '%s' has no pawn / world."), *GetPlayerName());
		return;
	}

	TSubclassOf<APawn> EnemyClass;
	FString KnownNames;
	for (const TPair<FName, TSoftClassPtr<APawn>>& Pair : Settings->DebugEnemyClasses)
	{
		KnownNames += Pair.Key.ToString() + TEXT(" ");
		if (Pair.Key.ToString().Equals(ShortName, ESearchCase::IgnoreCase))
		{
			EnemyClass = Pair.Value.LoadSynchronous();
			break;
		}
	}
	if (!EnemyClass)
	{
		UE_LOG(LogBHCombat, Warning, TEXT("bh.Enemy.Spawn: no enemy called '%s' (or its Blueprint is missing). Known: %s"), *ShortName, *KnownNames);
		return;
	}

	constexpr float SpawnDistance = 500.f; // 5 m in front of the player
	FVector Location = PlayerPawn->GetActorLocation() + PlayerPawn->GetActorForwardVector().GetSafeNormal2D() * SpawnDistance;
	if (UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World))
	{
		FNavLocation NavLocation;
		if (NavSys->ProjectPointToNavigation(Location, NavLocation, FVector(300.0, 300.0, 600.0)))
		{
			Location = NavLocation.Location;
		}
	}
	float LiftZ = 20.f;
	if (const ACharacter* ClassDefaults = Cast<ACharacter>(EnemyClass->GetDefaultObject()))
	{
		LiftZ += ClassDefaults->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	}
	Location.Z += LiftZ;
	const FRotator Facing(0.f, (PlayerPawn->GetActorLocation() - Location).GetSafeNormal2D().Rotation().Yaw, 0.f);

	APawn* Spawned = World->SpawnActorDeferred<APawn>(EnemyClass, FTransform(Facing, Location), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!Spawned)
	{
		UE_LOG(LogBHCombat, Warning, TEXT("bh.Enemy.Spawn: failed to spawn %s."), *GetNameSafe(EnemyClass));
		return;
	}
	if (ABH_EnemyBase* EnemyBase = Cast<ABH_EnemyBase>(Spawned))
	{
		EnemyBase->CombatTeam = EBH_CombatTeam::Enemies;
		EnemyBase->bResetOnDeath = false;
	}
	UBH_EnemyRankLibrary::PrepareSpawn(Spawned, RankOverride, LevelOverride);
	Spawned->FinishSpawning(FTransform(Facing, Location));

	UE_LOG(LogBHCombat, Log, TEXT("bh.Enemy.Spawn (from '%s'): %s -> %s rank %s / %s level %d at %s."), *GetPlayerName(), *ShortName, *Spawned->GetName(),
		UBH_EnemyRankLibrary::RankName(UBH_EnemyRankLibrary::GetRank(Spawned)), UBH_EnemyRankLibrary::ArchetypeName(UBH_EnemyRankLibrary::GetArchetype(Spawned)),
		UBH_EnemyRankLibrary::GetEnemyLevel(Spawned), *Location.ToCompactString());
}

bool ABH_PlayerState::ServerDebugKillAllEnemies_Validate(float RadiusCm)
{
	return RadiusCm >= 0.f && RadiusCm <= 1000000.f;
}

void ABH_PlayerState::ServerDebugKillAllEnemies_Implementation(float RadiusCm)
{
	UWorld* World = GetWorld();
	const APawn* PlayerPawn = GetPawn();
	if (!World)
	{
		return;
	}
	int32 Killed = 0;
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		APawn* Candidate = *It;
		if (!UBH_EnemyRankLibrary::HasRankData(Candidate))
		{
			continue;
		}
		if (RadiusCm > 0.f && PlayerPawn && FVector::Dist(Candidate->GetActorLocation(), PlayerPawn->GetActorLocation()) > RadiusCm)
		{
			continue;
		}
		UAbilitySystemComponent* ASC = Candidate->FindComponentByClass<UAbilitySystemComponent>();
		if (!ASC || ASC->HasMatchingGameplayTag(TAG_State_Combat_Dead) || !ASC->HasAttributeSetForAttribute(UAH_AttributeSet::GetHealthAttribute()))
		{
			continue;
		}
		// Lethal true damage through the normal pipeline (IncomingDamage -> Health -> OnHealthZero: XP, drops, ragdoll, wave bookkeeping).
		// A direct attribute override would skip PostGameplayEffectExecute, so the death path would never run.
		FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
		Context.AddInstigator(const_cast<APawn*>(PlayerPawn), const_cast<APawn*>(PlayerPawn));
		const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(UAH_GE_MeleeDamage::StaticClass(), 1.f, Context);
		if (Spec.IsValid() && Spec.Data.IsValid())
		{
			Spec.Data->SetSetByCallerMagnitude(TAG_Data_Damage, 1.0e6f);
			ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
		}
		++Killed;
	}
	UE_LOG(LogBHCombat, Log, TEXT("bh.Enemy.KillAll (from '%s'): %d enemies killed (radius %.0f cm)."), *GetPlayerName(), Killed, RadiusCm);
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
bool ABH_PlayerState::ServerDebugUnlockSetB_Validate() { return false; }
void ABH_PlayerState::ServerDebugUnlockSetB_Implementation() {}
bool ABH_PlayerState::ServerDebugGrantBurst_Validate() { return false; }
void ABH_PlayerState::ServerDebugGrantBurst_Implementation() {}
bool ABH_PlayerState::ServerDebugOpenSpanGate_Validate() { return false; }
void ABH_PlayerState::ServerDebugOpenSpanGate_Implementation() {}
bool ABH_PlayerState::ServerDebugSpawnEnemy_Validate(const FString&, int32, int32) { return false; }
void ABH_PlayerState::ServerDebugSpawnEnemy_Implementation(const FString&, int32, int32) {}
bool ABH_PlayerState::ServerDebugKillAllEnemies_Validate(float) { return false; }
void ABH_PlayerState::ServerDebugKillAllEnemies_Implementation(float) {}
#endif // !UE_BUILD_SHIPPING

// ============================================================================
// Partner panel data (13I-1)
// ============================================================================

void ABH_PlayerState::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (const UWorld* StateWorld = GetWorld())
	{
		StateWorld->GetTimerManager().ClearTimer(PartyStatusTimer);
	}
	if (Progression)
	{
		Progression->OnLevelUp.RemoveDynamic(this, &ABH_PlayerState::HandleStatusLevelUp);
	}
	UnbindPartyStatusSources();
	Super::EndPlay(EndPlayReason);
}

void ABH_PlayerState::OnRep_PartyStatus()
{
	OnPartyStatusChanged.Broadcast(PartyStatus);
}

void ABH_PlayerState::TickPartyStatus()
{
	APawn* CurrentPawn = GetPawn();
	// A new pawn (respawn), a lost pawn, or a pawn whose ASC was not ready last time: (re)bind the change sources.
	if (CurrentPawn != StatusPawn.Get() || (CurrentPawn && !StatusASC.IsValid()))
	{
		RebindPartyStatusSources(CurrentPawn);
	}

	if (!bPartyStatusDirty)
	{
		return;
	}
	bPartyStatusDirty = false;

	const FBH_PartyMemberStatus NewStatus = BuildPartyStatus();
	if (NewStatus != PartyStatus)
	{
		PartyStatus = NewStatus;
		ForceNetUpdate();
		OnPartyStatusChanged.Broadcast(PartyStatus); // the server does not get the RepNotify
	}
}

void ABH_PlayerState::UnbindPartyStatusSources()
{
	if (UAbilitySystemComponent* BoundASC = StatusASC.Get())
	{
		BoundASC->GetGameplayAttributeValueChangeDelegate(UAH_AttributeSet::GetHealthAttribute()).Remove(HealthHandle);
		BoundASC->GetGameplayAttributeValueChangeDelegate(UAH_AttributeSet::GetMaxHealthAttribute()).Remove(MaxHealthHandle);
		BoundASC->GetGameplayAttributeValueChangeDelegate(UAH_AttributeSet::GetBlightBuildupAttribute()).Remove(BlightHandle);
		BoundASC->RegisterGameplayTagEvent(TAG_State_Combat_InCombat, EGameplayTagEventType::NewOrRemoved).Remove(CombatTagHandle);
		BoundASC->RegisterGameplayTagEvent(TAG_State_Status_Fracture, EGameplayTagEventType::NewOrRemoved).Remove(FractureTagHandle);
	}
	if (UBH_PlayerDeathComponent* BoundDeath = StatusDeathComponent.Get())
	{
		BoundDeath->OnDeathPhaseChanged.RemoveDynamic(this, &ABH_PlayerState::HandleStatusDeathPhase);
	}
	HealthHandle.Reset();
	MaxHealthHandle.Reset();
	BlightHandle.Reset();
	CombatTagHandle.Reset();
	FractureTagHandle.Reset();
	StatusASC.Reset();
	StatusDeathComponent.Reset();
	StatusPawn.Reset();
}

void ABH_PlayerState::RebindPartyStatusSources(APawn* NewStatusPawn)
{
	UnbindPartyStatusSources();
	bPartyStatusDirty = true;
	if (!NewStatusPawn)
	{
		return;
	}

	UAbilitySystemComponent* PawnASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(NewStatusPawn);
	if (!PawnASC)
	{
		return; // StatusPawn stays unset, so the next tick retries
	}

	StatusPawn = NewStatusPawn;
	StatusASC = PawnASC;
	HealthHandle = PawnASC->GetGameplayAttributeValueChangeDelegate(UAH_AttributeSet::GetHealthAttribute()).AddUObject(this, &ABH_PlayerState::HandleStatusAttributeChanged);
	MaxHealthHandle = PawnASC->GetGameplayAttributeValueChangeDelegate(UAH_AttributeSet::GetMaxHealthAttribute()).AddUObject(this, &ABH_PlayerState::HandleStatusAttributeChanged);
	BlightHandle = PawnASC->GetGameplayAttributeValueChangeDelegate(UAH_AttributeSet::GetBlightBuildupAttribute()).AddUObject(this, &ABH_PlayerState::HandleStatusAttributeChanged);
	CombatTagHandle = PawnASC->RegisterGameplayTagEvent(TAG_State_Combat_InCombat, EGameplayTagEventType::NewOrRemoved).AddUObject(this, &ABH_PlayerState::HandleStatusTagChanged);
	FractureTagHandle = PawnASC->RegisterGameplayTagEvent(TAG_State_Status_Fracture, EGameplayTagEventType::NewOrRemoved).AddUObject(this, &ABH_PlayerState::HandleStatusTagChanged);

	if (UBH_PlayerDeathComponent* PawnDeath = UBH_PlayerDeathComponent::Find(NewStatusPawn))
	{
		StatusDeathComponent = PawnDeath;
		PawnDeath->OnDeathPhaseChanged.AddDynamic(this, &ABH_PlayerState::HandleStatusDeathPhase);
	}
}

void ABH_PlayerState::HandleStatusAttributeChanged(const FOnAttributeChangeData& ChangeData)
{
	MarkPartyStatusDirty();
}

void ABH_PlayerState::HandleStatusTagChanged(const FGameplayTag ChangedTag, int32 NewCount)
{
	MarkPartyStatusDirty();
}

void ABH_PlayerState::HandleStatusDeathPhase(EBH_DeathPhase NewPhase, EBH_DeathPhase OldPhase)
{
	MarkPartyStatusDirty();
}

void ABH_PlayerState::HandleStatusLevelUp(int32 NewLevel)
{
	MarkPartyStatusDirty();
}

FBH_PartyMemberStatus ABH_PlayerState::BuildPartyStatus() const
{
	FBH_PartyMemberStatus Built = PartyStatus; // values whose source is gone (no pawn yet) keep their last known value

	if (Progression)
	{
		Built.Level = FMath::Max(1, Progression->GetLevel());
	}

	if (const UAbilitySystemComponent* PawnASC = StatusASC.Get())
	{
		const float CurrentHealth = PawnASC->GetNumericAttribute(UAH_AttributeSet::GetHealthAttribute());
		const float CurrentMaxHealth = PawnASC->GetNumericAttribute(UAH_AttributeSet::GetMaxHealthAttribute());
		const float Fraction = CurrentMaxHealth > 0.f ? FMath::Clamp(CurrentHealth / CurrentMaxHealth, 0.f, 1.f) : 0.f;
		Built.HealthFraction = FMath::RoundToFloat(Fraction * 100.f) / 100.f;
		Built.BlightStacks = UBH_PartyLibrary::BlightBuildupToStacks(PawnASC->GetNumericAttribute(UAH_AttributeSet::GetBlightBuildupAttribute()));
		Built.bInCombat = PawnASC->HasMatchingGameplayTag(TAG_State_Combat_InCombat);
		Built.bFractured = PawnASC->HasMatchingGameplayTag(TAG_State_Status_Fracture);
	}

	if (const UBH_PlayerDeathComponent* PawnDeath = StatusDeathComponent.Get())
	{
		Built.DeathState = PawnDeath->GetPhase();
	}
	return Built;
}
