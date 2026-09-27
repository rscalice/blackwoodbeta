// Blackwood Hollow - Heart-Fragment ActorComponent (implementation)

#include "Components/BPC_HeartFragment.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemInterface.h"

UBPC_HeartFragment::UBPC_HeartFragment()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.f; // tick every frame; cheap float math only
	SetIsReplicatedByDefault(true);
}

void UBPC_HeartFragment::BeginPlay()
{
	Super::BeginPlay();

	EnsureAbilitySystemCached();
	CurrentBlightShield = MaxBlightShield;
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

	// Only the authority drives shield/mana/cooldown state; clients receive
	// the results via the AttributeSet's own replication and, for the shield
	// value (not itself a GAS attribute), can rely on OnBlightShieldChanged
	// firing when a server RPC or replicated property pushes an update if
	// this component is later marked to replicate CurrentBlightShield.
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

	// -- Passive mana regen -----------------------------------------------
	if (bDrivePassiveManaRegen && PassiveManaRegenPerSecond > 0.f)
	{
		ModifyMana(PassiveManaRegenPerSecond * DeltaTime);
	}

	// -- Overload Burst cooldown --------------------------------------------
	if (OverloadBurstCooldownRemaining > 0.f)
	{
		OverloadBurstCooldownRemaining = FMath::Max(0.f, OverloadBurstCooldownRemaining - DeltaTime);
		if (OverloadBurstCooldownRemaining <= 0.f)
		{
			OnOverloadBurstReady.Broadcast();

			if (EnsureAbilitySystemCached())
			{
				CachedASC->HandleGameplayEvent(FBH_GameplayTags::Get().Event_Combat_OverloadBurst_Ready, nullptr);
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

float UBPC_HeartFragment::GetCurrentMana() const
{
	return CachedAttributeSet ? CachedAttributeSet->GetMana() : 0.f;
}

float UBPC_HeartFragment::GetMaxMana() const
{
	return CachedAttributeSet ? CachedAttributeSet->GetMaxMana() : 0.f;
}

float UBPC_HeartFragment::GetManaPercent() const
{
	const float Max = GetMaxMana();
	return Max > 0.f ? GetCurrentMana() / Max : 0.f;
}

void UBPC_HeartFragment::ModifyMana(float Delta)
{
	if (FMath::IsNearlyZero(Delta) || !const_cast<UBPC_HeartFragment*>(this)->EnsureAbilitySystemCached())
	{
		return;
	}

	// UAH_AttributeSet's setters are protected via ATTRIBUTE_ACCESSORS friend
	// access; components go through the ASC's numeric attribute API so this
	// works whether Mana is being driven by GameplayEffects elsewhere too.
	const float CurrentValue = CachedAttributeSet->GetMana();
	const float MaxValue = CachedAttributeSet->GetMaxMana();
	const float NewValue = FMath::Clamp(CurrentValue + Delta, 0.f, MaxValue);

	CachedASC->ApplyModToAttribute(UAH_AttributeSet::GetManaAttribute(), EGameplayModOp::Override, NewValue);
}

bool UBPC_HeartFragment::HasEnoughMana(float RequiredMana) const
{
	return GetCurrentMana() >= RequiredMana;
}

bool UBPC_HeartFragment::TryActivateOverloadBurst()
{
	if (!IsOverloadBurstReady())
	{
		return false;
	}

	if (!HasEnoughMana(OverloadBurstManaCost))
	{
		return false;
	}

	if (!const_cast<UBPC_HeartFragment*>(this)->EnsureAbilitySystemCached())
	{
		return false;
	}

	ModifyMana(-OverloadBurstManaCost);
	OverloadBurstCooldownRemaining = OverloadBurstCooldown;

	const FBH_GameplayTags& Tags = FBH_GameplayTags::Get();
	CachedASC->AddLooseGameplayTag(Tags.State_Combat_Overloading);

	// GA_HeartFragment_OverloadBurst (a Blueprint UGameplayAbility asset, or
	// a native subclass of one) should be granted to this owner and set to
	// trigger off Event.Combat.OverloadBurst (Ability Triggers -> GameplayEvent).
	FGameplayEventData EventData;
	EventData.EventTag = Tags.Event_Combat_OverloadBurst;
	EventData.Instigator = GetOwner();
	EventData.Target = GetOwner();
	EventData.EventMagnitude = OverloadBurstManaCost;
	CachedASC->HandleGameplayEvent(Tags.Event_Combat_OverloadBurst, &EventData);

	OnOverloadBurstTriggered.Broadcast();
	return true;
}
