// Blackwood Hollow - Phase 9 RPG scaling settings (implementation)

#include "Progression/BH_RPGSettings.h"
#include "AbilitySystem/BH_GameplayTags.h"

namespace BH_RPGSettings_Private
{
	/** Curve table / row keys that already logged their "missing" warning (game thread only). */
	static TSet<FString> WarnedKeys;

	static void WarnOnce(const FString& Key, const FString& Message)
	{
		bool bAlreadyWarned = false;
		WarnedKeys.Add(Key, &bAlreadyWarned);
		if (!bAlreadyWarned)
		{
			UE_LOG(LogBHCombat, Warning, TEXT("RPG scaling: %s Falling back to the current values."), *Message);
		}
	}
}

UBH_RPGSettings::UBH_RPGSettings()
{
	CategoryName = TEXT("Game");
	PlayerScalingTable = TSoftObjectPtr<UCurveTable>(FSoftObjectPath(TEXT("/Game/BlackwoodHollow/Data/Scaling/CT_PlayerScaling.CT_PlayerScaling")));
	EnemyScalingTable = TSoftObjectPtr<UCurveTable>(FSoftObjectPath(TEXT("/Game/BlackwoodHollow/Data/Scaling/CT_EnemyScaling.CT_EnemyScaling")));
}

const UBH_RPGSettings* UBH_RPGSettings::Get()
{
	return GetDefault<UBH_RPGSettings>();
}

int32 UBH_RPGSettings::GetMaxLevel()
{
	return FMath::Max(Get()->MaxLevel, 1);
}

float UBH_RPGSettings::GetStaminaRegenMultiplier(EBH_ArmorWeightClass WeightClass) const
{
	switch (WeightClass)
	{
	case EBH_ArmorWeightClass::Medium: return MediumStaminaRegenMultiplier;
	case EBH_ArmorWeightClass::Heavy:  return HeavyStaminaRegenMultiplier;
	default:                           return ClothStaminaRegenMultiplier;
	}
}

float UBH_RPGSettings::GetDodgeDistanceMultiplier(EBH_ArmorWeightClass WeightClass) const
{
	switch (WeightClass)
	{
	case EBH_ArmorWeightClass::Medium: return MediumDodgeDistanceMultiplier;
	case EBH_ArmorWeightClass::Heavy:  return HeavyDodgeDistanceMultiplier;
	default:                           return ClothDodgeDistanceMultiplier;
	}
}

bool UBH_RPGSettings::EvaluateCurveRow(const TSoftObjectPtr<UCurveTable>& Table, FName RowName, float Level, float& OutValue)
{
	using namespace BH_RPGSettings_Private;

	UCurveTable* Loaded = Table.IsNull() ? nullptr : Table.LoadSynchronous();
	if (!Loaded)
	{
		WarnOnce(Table.ToString(), FString::Printf(TEXT("curve table '%s' is not set or could not be loaded (row '%s')."), *Table.ToString(), *RowName.ToString()));
		return false;
	}

	const FRealCurve* Curve = Loaded->FindCurve(RowName, FString(), /*bWarnIfNotFound*/ false);
	if (!Curve)
	{
		WarnOnce(Table.ToString() + TEXT("#") + RowName.ToString(), FString::Printf(TEXT("curve table '%s' has no row '%s'."), *Table.ToString(), *RowName.ToString()));
		return false;
	}

	OutValue = Curve->Eval(Level);
	return true;
}

bool UBH_RPGSettings::GetPlayerStat(FName RowName, int32 Level, float& OutValue) const
{
	return EvaluateCurveRow(PlayerScalingTable, RowName, static_cast<float>(Level), OutValue);
}

bool UBH_RPGSettings::GetEnemyStat(FName Prefix, FName Stat, int32 Level, float& OutValue) const
{
	if (Prefix.IsNone())
	{
		return false;
	}
	const FName RowName(*FString::Printf(TEXT("%s.%s"), *Prefix.ToString(), *Stat.ToString()));
	return EvaluateCurveRow(EnemyScalingTable, RowName, static_cast<float>(Level), OutValue);
}

int32 UBH_RPGSettings::GetXPToNextLevel(int32 Level) const
{
	if (Level >= GetMaxLevel())
	{
		return 0;
	}
	float Value = 0.f;
	if (GetPlayerStat(BH_ScalingRows::XPToNextLevel, Level, Value))
	{
		return FMath::Max(FMath::RoundToInt(Value), 1);
	}
	return FMath::Max(FallbackXPPerLevel, 1) * FMath::Max(Level, 1);
}
