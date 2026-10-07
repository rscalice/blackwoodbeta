// Blackwood Hollow - shared equipment stat helper (implementation)

#include "Items/BH_EquipmentStats.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystem/Effects/AH_GE_CombatEffects.h"
#include "AbilitySystemComponent.h"
#include "NarrativeItem.h"

const FString BH_EquipmentStats::Key_AttackPower = TEXT("AttackPowerBonus");
const FString BH_EquipmentStats::Key_Defense = TEXT("DefenseBonus");
const FString BH_EquipmentStats::Key_MaxStamina = TEXT("MaxStaminaBonus");

void BH_EquipmentStats::AddStatRows(TArray<FNarrativeItemStat>& OutStats)
{
	OutStats.Add(FNarrativeItemStat(NSLOCTEXT("BHEquipmentStats", "AttackPowerStat", "Attack Power"), Key_AttackPower));
	OutStats.Add(FNarrativeItemStat(NSLOCTEXT("BHEquipmentStats", "DefenseStat", "Defense"), Key_Defense));
	OutStats.Add(FNarrativeItemStat(NSLOCTEXT("BHEquipmentStats", "MaxStaminaStat", "Max Stamina"), Key_MaxStamina));
}

bool BH_EquipmentStats::GetStatString(const FString& VariableName, float AttackPower, float Defense, float MaxStamina, FString& OutValue)
{
	if (VariableName == Key_AttackPower)
	{
		OutValue = FString::SanitizeFloat(AttackPower);
		return true;
	}
	if (VariableName == Key_Defense)
	{
		OutValue = FString::SanitizeFloat(Defense);
		return true;
	}
	if (VariableName == Key_MaxStamina)
	{
		OutValue = FString::SanitizeFloat(MaxStamina);
		return true;
	}
	return false;
}

FActiveGameplayEffectHandle BH_EquipmentStats::Apply(UAbilitySystemComponent* ASC, UObject* Source, float AttackPower, float Defense, float MaxStamina)
{
	if (!ASC)
	{
		return FActiveGameplayEffectHandle();
	}
	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	Context.AddSourceObject(Source);
	FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(UAH_GE_EquipmentStatMod::StaticClass(), 1.f, Context);
	if (!Spec.IsValid())
	{
		return FActiveGameplayEffectHandle();
	}
	Spec.Data->SetSetByCallerMagnitude(TAG_Data_Equip_AttackPower, AttackPower);
	Spec.Data->SetSetByCallerMagnitude(TAG_Data_Equip_Defense, Defense);
	Spec.Data->SetSetByCallerMagnitude(TAG_Data_Equip_MaxStamina, MaxStamina);
	return ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
}

void BH_EquipmentStats::Remove(UAbilitySystemComponent* ASC, FActiveGameplayEffectHandle& Handle)
{
	if (ASC && Handle.IsValid())
	{
		ASC->RemoveActiveGameplayEffect(Handle);
	}
	Handle = FActiveGameplayEffectHandle();
}
