// Blackwood Hollow - Heart-Fragment ActorComponent (implementation)

#include "Components/BPC_HeartFragment.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystem/Abilities/AH_GA_FragmentBase.h"
#include "AbilitySystem/Abilities/AH_GA_OverloadBurst.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemInterface.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"

UBPC_HeartFragment::UBPC_HeartFragment()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.f; // tick every frame; cheap float math only
	SetIsReplicatedByDefault(true);

	// Default loadout: slot 1 = Overload Burst.
	EquippedFragments.Add(UAH_GA_OverloadBurst::StaticClass());
}

void UBPC_HeartFragment::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UBPC_HeartFragment, EquippedFragments);
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
	CurrentBlightShield = MaxBlightShield;

	TrimFragmentsToMax();

	const AActor* Owner = GetOwner();
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

void UBPC_HeartFragment::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Only the authority drives shield state.
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	// -- Blight shield regen --------------------------------------------
	if (CurrentBlightShield < MaxBlightShield)
	{
		TimeSinceLastShieldHit += DeltaTime;
		if (TimeSinceLastShieldHit >= BlightShieldRegenDelay)
		{
			const float Old = CurrentBlightShield;
			CurrentBlightShield = FMath::Min(MaxBlightShield, CurrentBlightShield + BlightShieldRegenPerSecond * DeltaTime);
			if (!FMath::IsNearlyEqual(Old, CurrentBlightShield))
			{
				OnBlightShieldChanged.Broadcast(CurrentBlightShield, MaxBlightShield);
			}
		}
	}
}

float UBPC_HeartFragment::AbsorbBlightDamage(float BlightDamage)
{
	if (BlightDamage <= 0.f)
	{
		return 0.f;
	}

	TimeSinceLastShieldHit = 0.f;

	if (CurrentBlightShield <= 0.f)
	{
		return BlightDamage;
	}

	const float Absorbed = FMath::Min(CurrentBlightShield, BlightDamage);
	CurrentBlightShield -= Absorbed;
	OnBlightShieldChanged.Broadcast(CurrentBlightShield, MaxBlightShield);

	const float Overflow = BlightDamage - Absorbed;

	if (CurrentBlightShield <= 0.f)
	{
		OnBlightShieldDepleted.Broadcast();

		if (EnsureAbilitySystemCached())
		{
			CachedASC->RemoveLooseGameplayTag(FBH_GameplayTags::Get().State_Combat_BlightShielded);

			FGameplayEventData EventData;
			EventData.EventTag = FBH_GameplayTags::Get().Event_Combat_BlightShieldDepleted;
			EventData.Instigator = GetOwner();
			EventData.Target = GetOwner();
			CachedASC->HandleGameplayEvent(FBH_GameplayTags::Get().Event_Combat_BlightShieldDepleted, &EventData);
		}
	}

	return Overflow;
}

void UBPC_HeartFragment::RechargeBlightShield()
{
	const bool bWasDepleted = CurrentBlightShield <= 0.f;
	CurrentBlightShield = MaxBlightShield;
	TimeSinceLastShieldHit = 0.f;
	OnBlightShieldChanged.Broadcast(CurrentBlightShield, MaxBlightShield);

	if (bWasDepleted && EnsureAbilitySystemCached())
	{
		CachedASC->AddLooseGameplayTag(FBH_GameplayTags::Get().State_Combat_BlightShielded);
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
