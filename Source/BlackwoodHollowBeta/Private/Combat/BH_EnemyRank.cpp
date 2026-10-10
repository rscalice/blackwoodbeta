// Blackwood Hollow - Phase 12G enemy rank / archetype helpers (implementation)

#include "Combat/BH_EnemyRank.h"
#include "Characters/BH_EnemyBase.h"
#include "Combat/BH_CombatIdentityComponent.h"
#include "Progression/BH_RPGSettings.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystemComponent.h"

FBH_EnemyRankMultipliers UBH_EnemyRankLibrary::GetMultipliers(EBH_EnemyRank Rank)
{
	const UBH_RPGSettings* Settings = UBH_RPGSettings::Get();
	return Settings ? Settings->GetRankMultipliers(Rank) : FBH_EnemyRankMultipliers();
}

EBH_EnemyRank UBH_EnemyRankLibrary::GetRank(const AActor* Enemy)
{
	if (const ABH_EnemyBase* Base = Cast<ABH_EnemyBase>(Enemy))
	{
		return Base->Rank;
	}
	if (const UBH_CombatIdentityComponent* Identity = UBH_CombatIdentityComponent::Find(Enemy))
	{
		return Identity->Rank;
	}
	return EBH_EnemyRank::Common;
}

EBH_EnemyArchetype UBH_EnemyRankLibrary::GetArchetype(const AActor* Enemy)
{
	if (const ABH_EnemyBase* Base = Cast<ABH_EnemyBase>(Enemy))
	{
		return Base->Archetype;
	}
	if (const UBH_CombatIdentityComponent* Identity = UBH_CombatIdentityComponent::Find(Enemy))
	{
		return Identity->Archetype;
	}
	return EBH_EnemyArchetype::Skirmisher;
}

bool UBH_EnemyRankLibrary::HasRankData(const AActor* Enemy)
{
	return Cast<ABH_EnemyBase>(Enemy) != nullptr || UBH_CombatIdentityComponent::Find(Enemy) != nullptr;
}

float UBH_EnemyRankLibrary::GetDamageMultiplier(const AActor* Enemy)
{
	return HasRankData(Enemy) ? GetMultipliers(GetRank(Enemy)).Damage : 1.f;
}

bool UBH_EnemyRankLibrary::IsBossPresented(const AActor* Enemy)
{
	if (const UBH_CombatIdentityComponent* Identity = UBH_CombatIdentityComponent::Find(Enemy))
	{
		if (Identity->bIsBoss)
		{
			return true;
		}
	}
	if (!HasRankData(Enemy))
	{
		return false;
	}
	const EBH_EnemyRank Rank = GetRank(Enemy);
	if (Rank == EBH_EnemyRank::Boss)
	{
		return true;
	}
	const UBH_RPGSettings* Settings = UBH_RPGSettings::Get();
	return Rank == EBH_EnemyRank::Elite && Settings && Settings->bEliteUsesBossBar;
}

int32 UBH_EnemyRankLibrary::GetEnemyLevel(const AActor* Enemy)
{
	if (const ABH_EnemyBase* Base = Cast<ABH_EnemyBase>(Enemy))
	{
		return Base->EnemyLevel;
	}
	if (const UBH_CombatIdentityComponent* Identity = UBH_CombatIdentityComponent::Find(Enemy))
	{
		return Identity->EnemyLevel;
	}
	return 1;
}

int32 UBH_EnemyRankLibrary::GetEnemyXPReward(const AActor* Enemy)
{
	if (const ABH_EnemyBase* Base = Cast<ABH_EnemyBase>(Enemy))
	{
		return Base->GetXPReward();
	}
	if (const UBH_CombatIdentityComponent* Identity = UBH_CombatIdentityComponent::Find(Enemy))
	{
		return Identity->GetXPReward();
	}
	return 0;
}

AActor* UBH_EnemyRankLibrary::GetAggroTarget(const AActor* Enemy)
{
	if (const ABH_EnemyBase* Base = Cast<ABH_EnemyBase>(Enemy))
	{
		return Base->GetAggroTarget();
	}
	if (const UBH_CombatIdentityComponent* Identity = UBH_CombatIdentityComponent::Find(Enemy))
	{
		return Identity->GetAggroTarget();
	}
	return nullptr;
}

FText UBH_EnemyRankLibrary::GetBossBarName(const AActor* Enemy)
{
	if (const UBH_CombatIdentityComponent* Identity = UBH_CombatIdentityComponent::Find(Enemy))
	{
		return Identity->GetBossBarName();
	}
	if (const ABH_EnemyBase* Base = Cast<ABH_EnemyBase>(Enemy))
	{
		return Base->DisplayName;
	}
	return FText::GetEmpty();
}

void UBH_EnemyRankLibrary::ApplyRankToStats(UAbilitySystemComponent* ASC, EBH_EnemyRank Rank)
{
	if (!ASC || !ASC->GetSet<UAH_AttributeSet>())
	{
		return;
	}
	const FBH_EnemyRankMultipliers Mult = GetMultipliers(Rank);

	if (!FMath::IsNearlyEqual(Mult.Health, 1.f))
	{
		const float NewMax = ASC->GetNumericAttributeBase(UAH_AttributeSet::GetMaxHealthAttribute()) * Mult.Health;
		ASC->SetNumericAttributeBase(UAH_AttributeSet::GetMaxHealthAttribute(), NewMax);
		ASC->SetNumericAttributeBase(UAH_AttributeSet::GetHealthAttribute(), NewMax);
	}
	if (!FMath::IsNearlyEqual(Mult.Posture, 1.f))
	{
		const float NewMax = ASC->GetNumericAttributeBase(UAH_AttributeSet::GetMaxPostureAttribute()) * Mult.Posture;
		ASC->SetNumericAttributeBase(UAH_AttributeSet::GetMaxPostureAttribute(), NewMax);
		ASC->SetNumericAttributeBase(UAH_AttributeSet::GetPostureAttribute(), NewMax);
	}
}

void UBH_EnemyRankLibrary::ApplyLevelScalingToASC(UAbilitySystemComponent* ASC, FName Prefix, int32 Level)
{
	if (!ASC || !ASC->GetSet<UAH_AttributeSet>())
	{
		return;
	}

	const int32 Lvl = FMath::Max(1, Level);
	ASC->SetNumericAttributeBase(UAH_AttributeSet::GetLevelAttribute(), static_cast<float>(Lvl));

	const UBH_RPGSettings* Settings = UBH_RPGSettings::Get();
	if (!Settings || Prefix.IsNone())
	{
		return;
	}

	float Value = 0.f;
	// Max first, then the current value (the attribute set clamps current to max).
	if (Settings->GetEnemyStat(Prefix, FName(BH_ScalingRows::MaxHealth), Lvl, Value))
	{
		ASC->SetNumericAttributeBase(UAH_AttributeSet::GetMaxHealthAttribute(), Value);
		ASC->SetNumericAttributeBase(UAH_AttributeSet::GetHealthAttribute(), Value);
	}
	if (Settings->GetEnemyStat(Prefix, FName(BH_ScalingRows::MaxPosture), Lvl, Value))
	{
		ASC->SetNumericAttributeBase(UAH_AttributeSet::GetMaxPostureAttribute(), Value);
		ASC->SetNumericAttributeBase(UAH_AttributeSet::GetPostureAttribute(), Value);
	}
	if (Settings->GetEnemyStat(Prefix, FName(BH_ScalingRows::AttackPower), Lvl, Value))
	{
		ASC->SetNumericAttributeBase(UAH_AttributeSet::GetAttackPowerAttribute(), Value);
	}
	if (Settings->GetEnemyStat(Prefix, FName(BH_ScalingRows::Defense), Lvl, Value))
	{
		ASC->SetNumericAttributeBase(UAH_AttributeSet::GetDefenseAttribute(), Value);
	}
}

int32 UBH_EnemyRankLibrary::ComputeXPReward(FName Prefix, int32 Level, int32 Fallback, EBH_EnemyRank Rank)
{
	float Base = static_cast<float>(FMath::Max(0, Fallback));
	const UBH_RPGSettings* Settings = UBH_RPGSettings::Get();
	float TableValue = 0.f;
	if (Settings && Settings->GetEnemyStat(Prefix, FName(BH_ScalingRows::XPReward), FMath::Max(1, Level), TableValue))
	{
		Base = TableValue;
	}
	return FMath::Max(0, FMath::RoundToInt(Base * GetMultipliers(Rank).XP));
}

void UBH_EnemyRankLibrary::PrepareSpawn(AActor* Spawned, int32 RankOverride, int32 LevelOverride)
{
	if (!Spawned)
	{
		return;
	}
	if (ABH_EnemyBase* Base = Cast<ABH_EnemyBase>(Spawned))
	{
		if (RankOverride >= 0)
		{
			Base->Rank = static_cast<EBH_EnemyRank>(FMath::Clamp(RankOverride, 0, 3));
		}
		if (LevelOverride > 0)
		{
			Base->EnemyLevel = LevelOverride;
		}
		return;
	}
	UBH_CombatIdentityComponent::SetPendingSpawn(Spawned, RankOverride, LevelOverride);
}

const TCHAR* UBH_EnemyRankLibrary::RankName(EBH_EnemyRank Rank)
{
	switch (Rank)
	{
	case EBH_EnemyRank::Veteran: return TEXT("Veteran");
	case EBH_EnemyRank::Elite:   return TEXT("Elite");
	case EBH_EnemyRank::Boss:    return TEXT("Boss");
	default:                     return TEXT("Common");
	}
}

const TCHAR* UBH_EnemyRankLibrary::ArchetypeName(EBH_EnemyArchetype Archetype)
{
	switch (Archetype)
	{
	case EBH_EnemyArchetype::Soldier:  return TEXT("Soldier");
	case EBH_EnemyArchetype::Brute:    return TEXT("Brute");
	case EBH_EnemyArchetype::Defender: return TEXT("Defender");
	case EBH_EnemyArchetype::Caster:   return TEXT("Caster");
	case EBH_EnemyArchetype::Swarm:    return TEXT("Swarm");
	default:                           return TEXT("Skirmisher");
	}
}

bool UBH_EnemyRankLibrary::ParseRank(const FString& Text, EBH_EnemyRank& OutRank)
{
	if (Text.Equals(TEXT("common"), ESearchCase::IgnoreCase))
	{
		OutRank = EBH_EnemyRank::Common;
	}
	else if (Text.Equals(TEXT("veteran"), ESearchCase::IgnoreCase) || Text.Equals(TEXT("vet"), ESearchCase::IgnoreCase))
	{
		OutRank = EBH_EnemyRank::Veteran;
	}
	else if (Text.Equals(TEXT("elite"), ESearchCase::IgnoreCase))
	{
		OutRank = EBH_EnemyRank::Elite;
	}
	else if (Text.Equals(TEXT("boss"), ESearchCase::IgnoreCase))
	{
		OutRank = EBH_EnemyRank::Boss;
	}
	else
	{
		return false;
	}
	return true;
}
