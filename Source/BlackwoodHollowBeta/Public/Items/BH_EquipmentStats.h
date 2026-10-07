// Blackwood Hollow - shared equipment stat helper (weapons and armor)
// Target: Unreal Engine 5.8 (C++), GAS
//
// Plain C++ helpers, NO reflection (the former USTRUCT FBH_EquipmentStats is gone). Weapon and armor items own three
// float UPROPERTYs (AttackPowerBonus / DefenseBonus / MaxStaminaBonus) and show them through Narrative's item Stats
// (UNarrativeItem::Stats + GetStringVariable). Apply / Remove are the single place that builds and applies/removes
// UAH_GE_EquipmentStatMod through its SetByCaller tags (Data.Equip.AttackPower / Defense / MaxStamina).

#pragma once

#include "CoreMinimal.h"
#include "ActiveGameplayEffectHandle.h"

class UAbilitySystemComponent;
class UObject;
struct FNarrativeItemStat;

namespace BH_EquipmentStats
{
	/** StringVariable keys used by the Narrative item Stats rows (match the UPROPERTY names on weapon and armor items). */
	extern BLACKWOODHOLLOWBETA_API const FString Key_AttackPower;
	extern BLACKWOODHOLLOWBETA_API const FString Key_Defense;
	extern BLACKWOODHOLLOWBETA_API const FString Key_MaxStamina;

	/** Appends the "Attack Power" / "Defense" / "Max Stamina" rows to a Narrative item's Stats array. */
	BLACKWOODHOLLOWBETA_API void AddStatRows(TArray<FNarrativeItemStat>& OutStats);

	/** Resolves a StringVariable key to its formatted value. @return false if VariableName is not one of the three keys. */
	BLACKWOODHOLLOWBETA_API bool GetStatString(const FString& VariableName, float AttackPower, float Defense, float MaxStamina, FString& OutValue);

	/** Server only: applies UAH_GE_EquipmentStatMod to ASC with the three bonuses (SourceObject = Source). @return the active handle (invalid on failure). */
	BLACKWOODHOLLOWBETA_API FActiveGameplayEffectHandle Apply(UAbilitySystemComponent* ASC, UObject* Source, float AttackPower, float Defense, float MaxStamina);

	/** Server only: removes the effect behind Handle (if valid) and resets it. */
	BLACKWOODHOLLOWBETA_API void Remove(UAbilitySystemComponent* ASC, FActiveGameplayEffectHandle& Handle);
}
