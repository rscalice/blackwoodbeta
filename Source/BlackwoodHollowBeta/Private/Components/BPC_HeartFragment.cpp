// Blackwood Hollow - Heart-Fragment ActorComponent (implementation)

#include "Components/BPC_HeartFragment.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystem/Abilities/AH_GA_FragmentBase.h"
#include "AbilitySystem/Abilities/AH_GA_OverloadBurst.h"
#include "AbilitySystem/StatusEffects/BH_BlightEffects.h"
#include "Player/BH_PlayerState.h"
#include "GameFramework/Pawn.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemInterface.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"

UBPC_HeartFragment::UBPC_HeartFragment()
{
	// Phase 10B: nothing ticks. The Blight meter decays on a timer that only exists while the meter is above zero.
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);

	// Default loadout: slot 1 = Overload Burst.
	EquippedFragments.Add(UAH_GA_OverloadBurst::StaticClass());
}

void UBPC_HeartFragment::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UBPC_HeartFragment, EquippedFragments);
	DOREPLIFETIME_CONDITION(UBPC_HeartFragment, ShieldingLevel, COND_OwnerOnly);
}

#if WITH_EDITOR
void UBPC_HeartFragment::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UBPC_HeartFragment, EquippedFragments)
		&& EquippedFragments.Num() > MaxFragmentSlots)
	{
		EquippedFragments.SetNum(MaxFragmentSlots);
	}
}
#endif

void UBPC_HeartFragment::BeginPlay()
{
	Super::BeginPlay();

	EnsureAbilitySystemCached();

	TrimFragmentsToMax();

	const AActor* Owner = GetOwner();
	if (Owner && Owner->HasAuthority())
	{
		// Phase 8B: the PlayerState owns the slot model. The pawn may be spawned before its PlayerState / ASC are ready,
		// so retry (also covers the legacy GrantEquippedFragments path below).
		if (!SyncFromPlayerState())
		{
			if (UWorld* World = GetWorld())
			{
				World->GetTimerManager().SetTimer(PlayerStateSyncTimer, this, &UBPC_HeartFragment::RetrySyncFromPlayerState, 0.25f, true);
			}
		}
	}
	if (Owner && Owner->HasAuthority() && !GrantEquippedFragments())
	{
		// The player's ASC is initialised by SetupCombatCharacter (which also calls GrantEquippedFragments);
		// retry shortly in case that hasn't run yet.
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(GrantRetryTimer, this, &UBPC_HeartFragment::RetryGrantFragments, 0.1f, true);
		}
	}
}

void UBPC_HeartFragment::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(GrantRetryTimer);
		World->GetTimerManager().ClearTimer(PlayerStateSyncTimer);
		World->GetTimerManager().ClearTimer(BlightDecayTimer);
	}
	Super::EndPlay(EndPlayReason);
}

void UBPC_HeartFragment::TrimFragmentsToMax()
{
	if (EquippedFragments.Num() > MaxFragmentSlots)
	{
		UE_LOG(LogTemp, Warning, TEXT("HeartFragment on '%s': %d fragments equipped, max is %d; trimming."),
			*GetNameSafe(GetOwner()), EquippedFragments.Num(), MaxFragmentSlots);
		EquippedFragments.SetNum(MaxFragmentSlots);
	}
}

UAbilitySystemComponent* UBPC_HeartFragment::GetOwnerASC() const
{
	return GetOwner() ? UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner()) : nullptr;
}

bool UBPC_HeartFragment::IsASCReady(const UAbilitySystemComponent* ASC)
{
	return ASC && ASC->AbilityActorInfo.IsValid() && ASC->AbilityActorInfo->AvatarActor.IsValid();
}

bool UBPC_HeartFragment::EnsureAbilitySystemCached()
{
	if (CachedASC && CachedAttributeSet)
	{
		return true;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}

	CachedASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Owner);
	if (!CachedASC)
	{
		return false;
	}

	CachedAttributeSet = CachedASC->GetSet<UAH_AttributeSet>();
	return CachedASC != nullptr && CachedAttributeSet != nullptr;
}

// ============================================================================
// Blight shielding
// ============================================================================

float UBPC_HeartFragment::GetShieldingFraction() const
{
	if (ShieldingByLevel.Num() == 0)
	{
		return 0.f;
	}
	const int32 Index = FMath::Clamp(ShieldingLevel, 0, ShieldingByLevel.Num() - 1);
	return FMath::Clamp(ShieldingByLevel[Index], 0.f, 1.f);
}

void UBPC_HeartFragment::SetShieldingLevel(int32 NewLevel)
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}
	NewLevel = FMath::Max(NewLevel, 0);
	if (NewLevel == ShieldingLevel)
	{
		return;
	}
	ShieldingLevel = NewLevel;
	OnShieldingLevelChanged.Broadcast(ShieldingLevel, GetShieldingFraction()); // the server / listen-server host does not get the RepNotify
}

void UBPC_HeartFragment::OnRep_ShieldingLevel(int32 OldLevel)
{
	OnShieldingLevelChanged.Broadcast(ShieldingLevel, GetShieldingFraction());
}

// ============================================================================
// Blight build-up meter
// ============================================================================

float UBPC_HeartFragment::ComputeMitigatedBuildup(float RawAmount) const
{
	if (RawAmount <= 0.f)
	{
		return 0.f;
	}

	// Defense-style resistance: 0 -> x1, 100 -> x0.5, 300 -> x0.25.
	const UAH_AttributeSet* AttributeSet = ResolveAttributeSet();
	const float Resistance = AttributeSet ? FMath::Max(AttributeSet->GetBlightResistance(), 0.f) : 0.f;
	const float Shielding = FMath::Clamp(GetShieldingFraction(), 0.f, 1.f);
	const float Aegis = FMath::Clamp(GetAegisReduction(), 0.f, 1.f);
	const float Other = GetOtherBlightReduction(); // Phase 11D: Warden sanctuary etc.
	return RawAmount * 100.f / (100.f + Resistance) * (1.f - Shielding) * (1.f - Aegis) * (1.f - Other);
}

void UBPC_HeartFragment::AddBlightReductionSource(const UObject* Source, float Fraction)
{
	if (!Source)
	{
		return;
	}
	OtherBlightReductions.Add(TWeakObjectPtr<const UObject>(Source), FMath::Clamp(Fraction, 0.f, 1.f));
}

void UBPC_HeartFragment::RemoveBlightReductionSource(const UObject* Source)
{
	if (!Source)
	{
		return;
	}
	OtherBlightReductions.Remove(TWeakObjectPtr<const UObject>(Source));
	// Drop sources that were destroyed without unregistering.
	for (auto It = OtherBlightReductions.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

float UBPC_HeartFragment::GetOtherBlightReduction() const
{
	float Remaining = 1.f;
	for (const TPair<TWeakObjectPtr<const UObject>, float>& Pair : OtherBlightReductions)
	{
		if (Pair.Key.IsValid())
		{
			Remaining *= (1.f - FMath::Clamp(Pair.Value, 0.f, 1.f));
		}
	}
	return FMath::Clamp(1.f - Remaining, 0.f, 1.f);
}

float UBPC_HeartFragment::ReduceBlightBuildup(float Amount)
{
	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority() || Amount <= 0.f || bSaturating)
	{
		return 0.f;
	}
	if (!EnsureAbilitySystemCached() || CachedASC->HasMatchingGameplayTag(TAG_State_Combat_Dead))
	{
		return 0.f;
	}
	const float Current = CachedAttributeSet->GetBlightBuildup();
	const float NewValue = FMath::Max(Current - Amount, 0.f);
	if (NewValue >= Current)
	{
		return 0.f;
	}
	SetBlightBuildupValue(NewValue);
	return Current - NewValue;
}

void UBPC_HeartFragment::DebugSetBlightBuildup(float Value)
{
#if !UE_BUILD_SHIPPING
	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority() || bSaturating || !EnsureAbilitySystemCached())
	{
		return;
	}
	const float Clamped = FMath::Clamp(Value, 0.f, UAH_AttributeSet::MaxBlightBuildup);
	if (Clamped >= UAH_AttributeSet::MaxBlightBuildup - KINDA_SMALL_NUMBER)
	{
		SaturateBlightMeter(nullptr);
		return;
	}
	if (const UWorld* World = GetWorld())
	{
		LastBlightGainTime = World->GetTimeSeconds(); // a fresh decay delay, like a real gain
	}
	SetBlightBuildupValue(Clamped);
	if (Clamped > 0.f)
	{
		EnsureBlightDecayTimer();
	}
	else
	{
		StopBlightDecayTimer();
	}
#else
	(void)Value;
#endif
}

float UBPC_HeartFragment::GetBlightBuildup() const
{
	const UAH_AttributeSet* AttributeSet = ResolveAttributeSet();
	return AttributeSet ? AttributeSet->GetBlightBuildup() : 0.f;
}

const UAH_AttributeSet* UBPC_HeartFragment::ResolveAttributeSet() const
{
	if (CachedAttributeSet)
	{
		return CachedAttributeSet;
	}
	const UAbilitySystemComponent* ASC = GetOwnerASC();
	return ASC ? ASC->GetSet<UAH_AttributeSet>() : nullptr;
}

int32 UBPC_HeartFragment::GetBlightStacks() const
{
	return FMath::Clamp(FMath::FloorToInt(GetBlightBuildup() / 10.f), 0, 10);
}

void UBPC_HeartFragment::SetBlightBuildupValue(float NewValue)
{
	if (CachedASC)
	{
		// The attribute set clamps to 0..MaxBlightBuildup (PreAttributeBaseChange) and replicates it to the owner.
		CachedASC->SetNumericAttributeBase(UAH_AttributeSet::GetBlightBuildupAttribute(), NewValue);
	}
}

float UBPC_HeartFragment::AddBlightBuildup(float RawAmount, AActor* InstigatorActor)
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || RawAmount <= 0.f || bSaturating)
	{
		return 0.f;
	}
	if (!EnsureAbilitySystemCached() || CachedASC->HasMatchingGameplayTag(TAG_State_Combat_Dead))
	{
		return 0.f;
	}

	const float Applied = ComputeMitigatedBuildup(RawAmount);
	if (Applied <= 0.f)
	{
		return 0.f;
	}

	if (const UWorld* World = GetWorld())
	{
		LastBlightGainTime = World->GetTimeSeconds(); // restarts the decay delay
	}

	const float NewValue = CachedAttributeSet->GetBlightBuildup() + Applied;
	if (NewValue >= UAH_AttributeSet::MaxBlightBuildup - KINDA_SMALL_NUMBER)
	{
		SaturateBlightMeter(InstigatorActor);
		return Applied;
	}

	SetBlightBuildupValue(NewValue);
	EnsureBlightDecayTimer();
	return Applied;
}

void UBPC_HeartFragment::EnsureBlightDecayTimer()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	FTimerManager& Timers = World->GetTimerManager();
	if (!Timers.IsTimerActive(BlightDecayTimer))
	{
		Timers.SetTimer(BlightDecayTimer, this, &UBPC_HeartFragment::BlightDecayTick, FMath::Max(BlightDecayTickInterval, 0.05f), true);
	}
}

void UBPC_HeartFragment::StopBlightDecayTimer()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BlightDecayTimer);
	}
}

void UBPC_HeartFragment::BlightDecayTick()
{
	const UWorld* World = GetWorld();
	if (!World || !EnsureAbilitySystemCached())
	{
		StopBlightDecayTimer();
		return;
	}

	const float Current = CachedAttributeSet->GetBlightBuildup();
	if (Current <= 0.f || CachedASC->HasMatchingGameplayTag(TAG_State_Combat_Dead))
	{
		StopBlightDecayTimer(); // idle (or dead: ResetBlight zeroes it)
		return;
	}

	// Decay starts BlightDecayDelay after the last gain; the elapsed time is measured from the later of that moment and the previous decay tick.
	const double Now = World->GetTimeSeconds();
	const double DecayFrom = FMath::Max(LastBlightDecayTime, LastBlightGainTime + BlightDecayDelay);
	const double Elapsed = Now - DecayFrom;
	if (Elapsed <= 0.0)
	{
		return; // still inside the delay
	}
	LastBlightDecayTime = Now;

	const float NewValue = FMath::Max(Current - BlightDecayPerSecond * static_cast<float>(Elapsed), 0.f);
	SetBlightBuildupValue(NewValue);
	if (NewValue <= 0.f)
	{
		StopBlightDecayTimer();
	}
}

void UBPC_HeartFragment::SaturateBlightMeter(AActor* InstigatorActor)
{
	if (bSaturating || !EnsureAbilitySystemCached())
	{
		return;
	}
	bSaturating = true;

	AActor* const Owner = GetOwner();
	UAbilitySystemComponent* const InstigatorASC = (InstigatorActor && InstigatorActor != Owner)
		? UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(InstigatorActor) : nullptr;

	// 1) Saturation damage THROUGH a GameplayEffect: IncomingDamage -> Health -> OnHealthZero / Dead tag / death event, like any hit.
	const float Damage = UAH_GE_BlightSaturationDamage::ApplySaturationDamage(InstigatorASC, CachedASC, InstigatorActor, BlightSaturationDamagePercent);
	const bool bSurvived = !CachedASC->HasMatchingGameplayTag(TAG_State_Combat_Dead);

	if (bSurvived)
	{
		FGameplayEventData SaturatedEvent;
		SaturatedEvent.EventTag = TAG_Event_Combat_BlightSaturated;
		SaturatedEvent.Instigator = InstigatorActor ? InstigatorActor : Owner;
		SaturatedEvent.Target = Owner;
		SaturatedEvent.EventMagnitude = Damage;
		CachedASC->HandleGameplayEvent(TAG_Event_Combat_BlightSaturated, &SaturatedEvent);

		// 2) Brief stagger: the existing hit-reaction path (UAH_GA_HitReaction triggers on Event.Combat.DamageReceived; Blight damage
		//    skips that event on purpose, so it is sent here). Hyper armor / posture break / death still suppress it.
		if (bStaggerOnSaturation && Damage > 0.f)
		{
			FGameplayEventData StaggerEvent;
			StaggerEvent.EventTag = TAG_Event_Combat_DamageReceived;
			StaggerEvent.Instigator = InstigatorActor ? InstigatorActor : Owner;
			StaggerEvent.Target = Owner;
			StaggerEvent.EventMagnitude = Damage;
			CachedASC->HandleGameplayEvent(TAG_Event_Combat_DamageReceived, &StaggerEvent);
		}

		// 3) Blight Rot (own source: the Rot is not credited to the instigator).
		if (CachedASC->HasMatchingGameplayTag(TAG_State_Combat_Dead) == false)
		{
			UAH_GE_BlightRot::ApplyBlightRot(nullptr, CachedASC, BlightRotDuration, BlightRotTickPercent, BlightRotStaminaRegenPenalty);
		}
	}

	// 4) Reset. (A death during step 1 leaves the meter at 0 as well; ResetBlight clears the Rot on revive.)
	StopBlightDecayTimer();
	SetBlightBuildupValue(0.f);
	OnBlightSaturated.Broadcast(Damage, bSurvived);

	bSaturating = false;
}

void UBPC_HeartFragment::ResetBlight()
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	StopBlightDecayTimer();
	bSaturating = false;
	LastBlightGainTime = -1.0e9;
	LastBlightDecayTime = -1.0e9;

	if (EnsureAbilitySystemCached())
	{
		SetBlightBuildupValue(0.f);
		UAH_GE_BlightRot::RemoveBlightRot(CachedASC);
	}
}

// ============================================================================
// Fragment loadout
// ============================================================================

void UBPC_HeartFragment::RetryGrantFragments()
{
	constexpr int32 MaxRetries = 100; // ~10 s
	++GrantRetryCount;

	if (GrantEquippedFragments())
	{
		GetWorld()->GetTimerManager().ClearTimer(GrantRetryTimer);
	}
	else if (GrantRetryCount >= MaxRetries)
	{
		GetWorld()->GetTimerManager().ClearTimer(GrantRetryTimer);
		UE_LOG(LogTemp, Warning, TEXT("HeartFragment on '%s': gave up granting fragments (no initialised AbilitySystemComponent)."), *GetNameSafe(GetOwner()));
	}
}

bool UBPC_HeartFragment::GrantEquippedFragments()
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return false;
	}
	if (bFragmentsGranted)
	{
		return true;
	}

	UAbilitySystemComponent* ASC = GetOwnerASC();
	if (!IsASCReady(ASC))
	{
		return false;
	}

	TrimFragmentsToMax();
	FragmentHandles.Reset();
	for (const TSubclassOf<UAH_GA_FragmentBase>& FragmentClass : EquippedFragments)
	{
		FGameplayAbilitySpecHandle Handle;
		if (FragmentClass)
		{
			if (const FGameplayAbilitySpec* Existing = ASC->FindAbilitySpecFromClass(FragmentClass))
			{
				Handle = Existing->Handle; // already granted (shared class / duplicate call): don't double-grant
			}
			else
			{
				Handle = ASC->GiveAbility(FGameplayAbilitySpec(FragmentClass, 1, INDEX_NONE, GetOwner()));
			}
		}
		FragmentHandles.Add(Handle);
	}

	bFragmentsGranted = true;
	return true;
}

void UBPC_HeartFragment::RetrySyncFromPlayerState()
{
	constexpr int32 MaxRetries = 80; // ~20 s
	++PlayerStateSyncRetryCount;

	if (SyncFromPlayerState() || PlayerStateSyncRetryCount >= MaxRetries)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(PlayerStateSyncTimer);
		}
	}
}

bool UBPC_HeartFragment::SyncFromPlayerState()
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return false;
	}

	const APawn* OwnerPawn = Cast<APawn>(Owner);
	const ABH_PlayerState* PS = OwnerPawn ? Cast<ABH_PlayerState>(OwnerPawn->GetPlayerState()) : nullptr;
	if (!PS || !IsASCReady(GetOwnerASC()))
	{
		return false;
	}

	GrantEquippedFragments();
	for (int32 SlotIndex = 0; SlotIndex < MaxFragmentSlots; ++SlotIndex)
	{
		SetFragmentInSlot(SlotIndex, PS->GetFragmentInSlot(SlotIndex));
	}
	bSyncedFromPlayerState = true;
	return true;
}

bool UBPC_HeartFragment::SetFragmentInSlot(int32 Slot, TSubclassOf<UAH_GA_FragmentBase> FragmentClass)
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || Slot < 0 || Slot >= MaxFragmentSlots)
	{
		return false;
	}

	UAbilitySystemComponent* ASC = GetOwnerASC();
	if (!IsASCReady(ASC))
	{
		return false;
	}

	// Make sure the existing loadout is granted first so the parallel handle array is valid.
	GrantEquippedFragments();

	while (EquippedFragments.Num() <= Slot)
	{
		EquippedFragments.Add(nullptr);
	}
	while (FragmentHandles.Num() < EquippedFragments.Num())
	{
		FragmentHandles.Add(FGameplayAbilitySpecHandle());
	}

	const TSubclassOf<UAH_GA_FragmentBase> OldClass = EquippedFragments[Slot];
	if (OldClass == FragmentClass)
	{
		return true;
	}

	EquippedFragments[Slot] = FragmentClass;
	FragmentHandles[Slot] = FGameplayAbilitySpecHandle();

	// Clear the old spec unless another slot still uses that class.
	if (OldClass && !EquippedFragments.Contains(OldClass))
	{
		if (const FGameplayAbilitySpec* OldSpec = ASC->FindAbilitySpecFromClass(OldClass))
		{
			ASC->ClearAbility(OldSpec->Handle);
		}
	}

	if (FragmentClass)
	{
		if (const FGameplayAbilitySpec* Existing = ASC->FindAbilitySpecFromClass(FragmentClass))
		{
			FragmentHandles[Slot] = Existing->Handle;
		}
		else
		{
			FragmentHandles[Slot] = ASC->GiveAbility(FGameplayAbilitySpec(FragmentClass, 1, INDEX_NONE, GetOwner()));
		}
	}

	// Keep the replicated array's trailing nulls trimmed to something sensible.
	while (EquippedFragments.Num() > 0 && !EquippedFragments.Last())
	{
		EquippedFragments.Pop();
		FragmentHandles.SetNum(EquippedFragments.Num());
	}
	return true;
}

FGameplayAbilitySpecHandle UBPC_HeartFragment::ResolveFragmentHandle(int32 Slot) const
{
	const TSubclassOf<UAH_GA_FragmentBase> FragmentClass = GetFragmentClass(Slot);
	if (!FragmentClass)
	{
		return FGameplayAbilitySpecHandle();
	}

	const UAbilitySystemComponent* ASC = GetOwnerASC();
	if (!ASC)
	{
		return FGameplayAbilitySpecHandle();
	}

	// Server: use the stored handle if it is still a live spec.
	if (FragmentHandles.IsValidIndex(Slot) && FragmentHandles[Slot].IsValid()
		&& const_cast<UAbilitySystemComponent*>(ASC)->FindAbilitySpecFromHandle(FragmentHandles[Slot]))
	{
		return FragmentHandles[Slot];
	}

	// Client (or stale handle): specs replicate via ActivatableAbilities, so look the spec up by class.
	if (const FGameplayAbilitySpec* Spec = const_cast<UAbilitySystemComponent*>(ASC)->FindAbilitySpecFromClass(FragmentClass))
	{
		return Spec->Handle;
	}
	return FGameplayAbilitySpecHandle();
}

bool UBPC_HeartFragment::TryActivateFragment(int32 Slot)
{
	UAbilitySystemComponent* ASC = GetOwnerASC();
	if (!ASC)
	{
		return false;
	}

	const FGameplayAbilitySpecHandle Handle = ResolveFragmentHandle(Slot);
	if (!Handle.IsValid())
	{
		return false;
	}
	return ASC->TryActivateAbility(Handle);
}

void UBPC_HeartFragment::GetFragmentCooldown(int32 Slot, float& Remaining, float& Duration) const
{
	Remaining = 0.f;
	Duration = 0.f;
	UAH_GA_FragmentBase::GetCooldownRemaining(GetOwnerASC(), GetFragmentClass(Slot), Remaining, Duration);
}

TSubclassOf<UAH_GA_FragmentBase> UBPC_HeartFragment::GetFragmentClass(int32 Slot) const
{
	return (Slot >= 0 && Slot < MaxFragmentSlots && EquippedFragments.IsValidIndex(Slot)) ? EquippedFragments[Slot] : nullptr;
}
