// Blackwood Hollow - Phase 8C equipment types (implementation)

#include "Items/BH_EquipmentTypes.h"
#include "Items/BH_WeaponItem.h"
#include "Combat/BH_LoadoutComponent.h"
#include "GameFramework/Pawn.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystem/Effects/AH_GE_CombatEffects.h"
#include "AbilitySystemComponent.h"
#include "NarrativeItem.h"

EEquippableSlot UBH_EquipmentLibrary::ToEquippableSlot(EBH_EquipSlot Slot)
{
	switch (Slot)
	{
	case EBH_EquipSlot::Head:          return EEquippableSlot::ES_Helmet;
	case EBH_EquipSlot::Chest:         return EEquippableSlot::ES_Torso;
	case EBH_EquipSlot::Arms:          return EEquippableSlot::ES_Hands;
	case EBH_EquipSlot::Legs:          return EEquippableSlot::ES_Legs;
	case EBH_EquipSlot::Feet:          return EEquippableSlot::ES_Feet;
	case EBH_EquipSlot::Weapon_Main_A: return EEquippableSlot::ES_Weapon;
	case EBH_EquipSlot::Weapon_Off_A:  return EEquippableSlot::ES_Holster;
	case EBH_EquipSlot::Weapon_Main_B: return EEquippableSlot::ES_Custom1;
	case EBH_EquipSlot::Weapon_Off_B:  return EEquippableSlot::ES_Custom2;
	default:                           return EEquippableSlot::ES_Weapon;
	}
}

bool UBH_EquipmentLibrary::FromEquippableSlot(EEquippableSlot Slot, EBH_EquipSlot& OutSlot)
{
	switch (Slot)
	{
	case EEquippableSlot::ES_Helmet:  OutSlot = EBH_EquipSlot::Head;          return true;
	case EEquippableSlot::ES_Torso:   OutSlot = EBH_EquipSlot::Chest;         return true;
	case EEquippableSlot::ES_Hands:   OutSlot = EBH_EquipSlot::Arms;          return true;
	case EEquippableSlot::ES_Legs:    OutSlot = EBH_EquipSlot::Legs;          return true;
	case EEquippableSlot::ES_Feet:    OutSlot = EBH_EquipSlot::Feet;          return true;
	case EEquippableSlot::ES_Weapon:  OutSlot = EBH_EquipSlot::Weapon_Main_A; return true;
	case EEquippableSlot::ES_Holster: OutSlot = EBH_EquipSlot::Weapon_Off_A;  return true;
	case EEquippableSlot::ES_Custom1: OutSlot = EBH_EquipSlot::Weapon_Main_B; return true;
	case EEquippableSlot::ES_Custom2: OutSlot = EBH_EquipSlot::Weapon_Off_B;  return true;
	default:                          return false;
	}
}

bool UBH_EquipmentLibrary::IsWeaponSlot(EBH_EquipSlot Slot)
{
	return Slot == EBH_EquipSlot::Weapon_Main_A || Slot == EBH_EquipSlot::Weapon_Off_A
		|| Slot == EBH_EquipSlot::Weapon_Main_B || Slot == EBH_EquipSlot::Weapon_Off_B;
}

bool UBH_EquipmentLibrary::IsOffHandSlot(EBH_EquipSlot Slot)
{
	return Slot == EBH_EquipSlot::Weapon_Off_A || Slot == EBH_EquipSlot::Weapon_Off_B;
}

EBH_LoadoutSet UBH_EquipmentLibrary::GetSlotLoadoutSet(EBH_EquipSlot Slot)
{
	return (Slot == EBH_EquipSlot::Weapon_Main_B || Slot == EBH_EquipSlot::Weapon_Off_B) ? EBH_LoadoutSet::B : EBH_LoadoutSet::A;
}

EBH_EquipSlot UBH_EquipmentLibrary::GetMainSlot(EBH_LoadoutSet Set)
{
	return Set == EBH_LoadoutSet::A ? EBH_EquipSlot::Weapon_Main_A : EBH_EquipSlot::Weapon_Main_B;
}

EBH_EquipSlot UBH_EquipmentLibrary::GetOffSlot(EBH_LoadoutSet Set)
{
	return Set == EBH_LoadoutSet::A ? EBH_EquipSlot::Weapon_Off_A : EBH_EquipSlot::Weapon_Off_B;
}

bool UBH_EquipmentLibrary::EquipWeaponToSlot(APawn* Pawn, UBH_WeaponItem* Item, EBH_EquipSlot Slot, FText& OutReason)
{
	if (UBH_LoadoutComponent* Loadout = UBH_LoadoutComponent::FindLoadoutComponent(Pawn))
	{
		return Loadout->EquipWeaponToSlot(Item, Slot, OutReason);
	}
	OutReason = FText::FromString(TEXT("Pawn has no loadout component"));
	return false;
}

bool UBH_EquipmentLibrary::UnequipWeaponSlot(APawn* Pawn, EBH_EquipSlot Slot, FText& OutReason)
{
	if (UBH_LoadoutComponent* Loadout = UBH_LoadoutComponent::FindLoadoutComponent(Pawn))
	{
		return Loadout->UnequipWeaponSlot(Slot, OutReason);
	}
	OutReason = FText::FromString(TEXT("Pawn has no loadout component"));
	return false;
}

// ============================================================================
// Equipment stat bonuses
// ============================================================================

void UBH_EquipmentLibrary::AddStatRows(TArray<FNarrativeItemStat>& OutStats)
{
	OutStats.Add(FNarrativeItemStat(NSLOCTEXT("BHEquipmentStats", "AttackPowerStat", "Attack Power"), FString(BH_EquipStatKeys::AttackPower)));
	OutStats.Add(FNarrativeItemStat(NSLOCTEXT("BHEquipmentStats", "DefenseStat", "Defense"), FString(BH_EquipStatKeys::Defense)));
	OutStats.Add(FNarrativeItemStat(NSLOCTEXT("BHEquipmentStats", "MaxStaminaStat", "Max Stamina"), FString(BH_EquipStatKeys::MaxStamina)));
}

bool UBH_EquipmentLibrary::GetStatString(const FString& VariableName, float AttackPower, float Defense, float MaxStamina, FString& OutValue)
{
	if (VariableName == BH_EquipStatKeys::AttackPower)
	{
		OutValue = FString::SanitizeFloat(AttackPower);
		return true;
	}
	if (VariableName == BH_EquipStatKeys::Defense)
	{
		OutValue = FString::SanitizeFloat(Defense);
		return true;
	}
	if (VariableName == BH_EquipStatKeys::MaxStamina)
	{
		OutValue = FString::SanitizeFloat(MaxStamina);
		return true;
	}
	return false;
}

FActiveGameplayEffectHandle UBH_EquipmentLibrary::ApplyStatMod(UAbilitySystemComponent* ASC, UObject* Source, float AttackPower, float Defense, float MaxStamina)
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

void UBH_EquipmentLibrary::RemoveStatMod(UAbilitySystemComponent* ASC, FActiveGameplayEffectHandle& Handle)
{
	if (ASC && Handle.IsValid())
	{
		ASC->RemoveActiveGameplayEffect(Handle);
	}
	Handle = FActiveGameplayEffectHandle();
}
