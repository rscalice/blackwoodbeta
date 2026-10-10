// Blackwood Hollow - Native Gameplay Tag hierarchy
// Target: Unreal Engine 5.8 (C++)
//
// Follows the Lyra-style "singleton struct" pattern: a single FBH_GameplayTags
// instance owns every native FGameplayTag used by combat, stance, and event
// systems, initialized once at module startup via InitializeNativeTags().
//
// Usage:
//   const FBH_GameplayTags& Tags = FBH_GameplayTags::Get();
//   AbilitySystemComponent->AddLooseGameplayTag(Tags.State_Combat_InCombat);

#pragma once

#include "NativeGameplayTags.h"

/** Combat log channel (dodge i-frame whiffs, stamina spends, ...). Verbose messages are opt-in: Log LogBHCombat Verbose. */
BLACKWOODHOLLOWBETA_API DECLARE_LOG_CATEGORY_EXTERN(LogBHCombat, Log, All);

// ---------------------------------------------------------------------------
// Weapon stance lives in the Stance.Weapon.* tags declared near the end of this header
// (source of truth: UBH_StanceComponent::CurrentStance on ABH_CharacterBase descendants).
// The legacy GASPALS OverlayPose path keeps working through the FName keys; see BH_Stance below.
// ---------------------------------------------------------------------------
// State.Combat.*  -- persistent/loose tags describing current combat state
// ---------------------------------------------------------------------------
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Combat_InCombat);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Combat_Blocking);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Combat_Parrying);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Combat_Staggered);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Combat_PostureBroken);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Combat_Overloading);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Combat_Dead);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Combat_Attacking);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Combat_HyperArmor);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Combat_ComboWindow);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Combat_PostureRegenDelayed);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Combat_MovementLocked);

// ---------------------------------------------------------------------------
// Event.Combat.*  -- momentary gameplay events sent via SendGameplayEventToActor
// ---------------------------------------------------------------------------
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Event_Combat_Hit);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Event_Combat_PostureBreak);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Event_Combat_Death);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Event_Combat_BlightDamage);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Event_Combat_OverloadBurst);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Event_Combat_OverloadBurst_Ready);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Event_Combat_DamageReceived);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Event_Combat_HitDealt);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Event_Combat_ComboWindow_Open);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Event_Combat_ComboWindow_Close);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Event_Combat_Input_Attack);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Event_Combat_Parry_Success);

// ---------------------------------------------------------------------------
// Ability.* / Damage.* / Data.*  -- ability identity, damage typing, SetByCaller keys
// ---------------------------------------------------------------------------
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Ability_Combat_MeleeAttack);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Ability_Combat_Parry);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Damage_Type_Melee);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Data_Damage);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Data_PostureDamage);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Event_Combat_BlockImpact);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Ability_Combat_Block);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Ability_Combat_HitReaction);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Ability_Combat_PostureBreak);

// ---------------------------------------------------------------------------
// Phase 6: Flurry / Riposte / Shield Bash / GameplayCues
// ---------------------------------------------------------------------------
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Combat_Flurry);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Combat_RiposteReady);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Ability_Combat_ShieldBash);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Cooldown_Combat_ShieldBash);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_GameplayCue_Combat_Hit);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_GameplayCue_Combat_ParrySuccess);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_GameplayCue_Combat_PostureBroken);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_GameplayCue_Combat_Hit_ShieldBash);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Combat_HitResult_Blocked);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Combat_HitResult_Fatal);

// ---------------------------------------------------------------------------
// Phase 7A: Dodge / Stamina
// ---------------------------------------------------------------------------
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Combat_Dodging);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Combat_Invulnerable);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Combat_StaminaRegenDelayed);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Ability_Combat_Dodge);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Data_StaminaCost);

// ---------------------------------------------------------------------------
// Phase 7C: Heart-Fragment loadout / generic cooldowns
// ---------------------------------------------------------------------------
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Data_Cooldown);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Cooldown_Fragment_OverloadBurst);

// ---------------------------------------------------------------------------
// Phase 8C: equipment stat-mod SetByCaller keys (read by UAH_GE_EquipmentStatMod)
// ---------------------------------------------------------------------------
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Data_Equip_AttackPower);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Data_Equip_Defense);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Data_Equip_MaxStamina);

// ---------------------------------------------------------------------------
// Phase 9: RPG scaling -- damage formula SetByCaller keys, armor weight class
// ---------------------------------------------------------------------------
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Data_DamageMultiplier);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Data_AttackPowerScale);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Data_Equip_StaminaRegenMult);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Armor_Weight_Medium);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Armor_Weight_Heavy);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_GameplayCue_Player_LevelUp);

// ---------------------------------------------------------------------------
// Phase 3: Stance.Weapon.* -- weapon stance (source of truth: UBH_StanceComponent::CurrentStance, replicated;
// mirrored as a loose tag on the ASC on every machine)
// ---------------------------------------------------------------------------
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Stance_Weapon);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Stance_Weapon_Unarmed);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Stance_Weapon_Greatsword);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Stance_Weapon_SwordShield);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Stance_Weapon_DualSword);
// Phase 12F-2: the one-handed Sword is a VARIANT of Sword & Shield (same locomotion / dodge / hit reactions). This tag is only a
// lookup KEY (loadout mesh entry, guard / parry montages, block drain, melee ability); it is never a CurrentStance.
// See UBH_StanceComponent::GetStanceKey and BH_Stance::IsVariantKeyStance.
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Stance_Weapon_Sword);
// Phase 8D placeholder stances (selectable in the UI but not implemented yet; see BH_Stance::IsPlaceholderStance)
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Stance_Weapon_Bow);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Stance_Weapon_Crossbow);

// ---------------------------------------------------------------------------
// Phase 8B: Heart-Fragment slot categories (a fragment ability carries one; a slot restricts to one)
// ---------------------------------------------------------------------------
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Fragment_Category_Vitality);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Fragment_Category_Offensive);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Fragment_Category_BlightResist);

// ---------------------------------------------------------------------------
// Phase 8C: Blight damage type
// ---------------------------------------------------------------------------
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Damage_Type_Blight);

// ---------------------------------------------------------------------------
// Phase 10B: Blight build-up meter / status-effect framework
//   State.Status.*  -- every status effect (UBH_GE_StatusEffect) grants one; UBH_StatusEffectLibrary lists them.
//   Data.Blight.*   -- SetByCaller keys of the Blight Rot / saturation effects (StatusEffects/BH_BlightEffects.h)
//   Event.Combat.BlightSaturated -- the meter filled up (server), sent to the victim after the saturation damage.
// ---------------------------------------------------------------------------
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Status);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Status_BlightRot);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Data_Blight_RotDamagePercent);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Data_Blight_RotStaminaRegenMult);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Data_Blight_SaturationPercent);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Event_Combat_BlightSaturated);

// Phase 11D: consumables (Heartwood Sap, Warden's Incense)
//   State.Action.Consuming        granted by UBH_GA_UseConsumable while the use animation runs; attack / dodge / block / parry / shield bash / hit reaction refuse to start while it is present.
//   Ability.Consumable.Use        asset tag of UBH_GA_UseConsumable (posture break cancels abilities carrying it).
//   Event.Consumable.Use          gameplay event that activates UBH_GA_UseConsumable; payload OptionalObject = the item class to use.
//   State.Status.HeartwoodSap / WardenSanctuary   status tags of the two consumable effects (UBH_GE_StatusEffect).
//   Data.Consumable.*             SetByCaller keys of the consumable effects.
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Action_Consuming);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Ability_Consumable_Use);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Event_Consumable_Use);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Status_HeartwoodSap);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Status_WardenSanctuary);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Data_Consumable_HealFractionPerTick);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Data_Consumable_PostureRegenMult);

// Weapon drawn / sheathed (UBH_StanceComponent::bWeaponDrawn, replicated; mirrored as loose tags on every machine, exactly one present)
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Weapon_Drawn);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Weapon_Sheathed);

namespace BH_Stance
{
	/** Legacy Enum_OverlayPose / loadout key -> tag. Accepts "SwordAndShield", "SwordShield", "Sword&Shield", "DualSword", "Greatsword", "Unarmed" (case-insensitive). Unknown -> empty tag. */
	BLACKWOODHOLLOWBETA_API FGameplayTag FromLegacyName(FName LegacyName);

	/** Tag -> legacy key used by the TMap<FName,...> ability maps and the weapon loadout asset: SwordShield -> "SwordAndShield", others by name. Non-stance tag -> NAME_None. */
	BLACKWOODHOLLOWBETA_API FName ToLegacyName(FGameplayTag StanceTag);

	/** Tag is a child of Stance.Weapon (the parent itself does not count). */
	BLACKWOODHOLLOWBETA_API bool IsWeaponStance(FGameplayTag Tag);

	/** True for the variant KEY tags (Stance.Weapon.Sword): valid lookup keys, never a stance that can be set. */
BLACKWOODHOLLOWBETA_API bool IsVariantKeyStance(FGameplayTag Tag);

/** Phase 8D: Bow / Crossbow are listed in the UI but have no abilities or animations yet. */
	BLACKWOODHOLLOWBETA_API bool IsPlaceholderStance(FGameplayTag Tag);

	/** Unarmed, Greatsword, SwordShield, DualSword (implemented stances only; placeholders are excluded). */
	BLACKWOODHOLLOWBETA_API const TArray<FGameplayTag>& AllWeaponStances();

	/** Tag-keyed map first, legacy FName-keyed map as the fallback (Phase 6 migration; the legacy maps are removed in Phase 7). */
	template <typename T>
	const T* FindForStance(const TMap<FGameplayTag, T>& TagMap, const TMap<FName, T>& LegacyMap, FGameplayTag Stance)
	{
		if (const T* Found = TagMap.Find(Stance))
		{
			return Found;
		}
		const FName Legacy = ToLegacyName(Stance);
		return Legacy.IsNone() ? nullptr : LegacyMap.Find(Legacy);
	}
}

/**
 * Singleton accessor kept for readability at call sites and for parity with
 * the Lyra FGameplayTags pattern many UE5 GAS codebases already follow.
 * The UE_DECLARE_GAMEPLAY_TAG_EXTERN macros above are what actually register
 * the tags with the GameplayTags system (see the .cpp for UE_DEFINE_GAMEPLAY_TAG).
 * This struct just gives you named, typo-proof access to them at runtime.
 */
struct BLACKWOODHOLLOWBETA_API FBH_GameplayTags
{
public:
	static const FBH_GameplayTags& Get() { return GameplayTags; }
	static void InitializeNativeTags();

	// State.Combat
	FGameplayTag State_Combat_InCombat;
	FGameplayTag State_Combat_Blocking;
	FGameplayTag State_Combat_Parrying;
	FGameplayTag State_Combat_Staggered;
	FGameplayTag State_Combat_PostureBroken;
	FGameplayTag State_Combat_Overloading;
	FGameplayTag State_Combat_Dead;
	FGameplayTag State_Combat_Attacking;
	FGameplayTag State_Combat_ComboWindow;
	FGameplayTag State_Combat_PostureRegenDelayed;

	// Event.Combat
	FGameplayTag Event_Combat_Hit;
	FGameplayTag Event_Combat_PostureBreak;
	FGameplayTag Event_Combat_Death;
	FGameplayTag Event_Combat_BlightDamage;
	FGameplayTag Event_Combat_OverloadBurst;
	FGameplayTag Event_Combat_OverloadBurst_Ready;
	FGameplayTag Event_Combat_DamageReceived;
	FGameplayTag Event_Combat_HitDealt;
	FGameplayTag Event_Combat_ComboWindow_Open;
	FGameplayTag Event_Combat_ComboWindow_Close;
	FGameplayTag Event_Combat_Input_Attack;
	FGameplayTag Event_Combat_Parry_Success;

	// Ability / Damage / Data
	FGameplayTag Ability_Combat_MeleeAttack;
	FGameplayTag Ability_Combat_Parry;
	FGameplayTag Damage_Type_Melee;
	FGameplayTag Data_Damage;
	FGameplayTag Data_PostureDamage;
	FGameplayTag Event_Combat_BlockImpact;
	FGameplayTag Ability_Combat_Block;
	FGameplayTag Ability_Combat_HitReaction;
	FGameplayTag Ability_Combat_PostureBreak;

	// Phase 7C
	FGameplayTag Data_Cooldown;
	FGameplayTag Cooldown_Fragment_OverloadBurst;

	// Phase 3 Stance.Weapon
	FGameplayTag Stance_Weapon_Unarmed;
	FGameplayTag Stance_Weapon_Greatsword;
	FGameplayTag Stance_Weapon_SwordShield;
	FGameplayTag Stance_Weapon_DualSword;

	// Weapon state
	FGameplayTag State_Weapon_Drawn;
	FGameplayTag State_Weapon_Sheathed;

protected:
	void AddAllTags();
	void AddTag(FGameplayTag& OutTag, const ANSICHAR* TagName, const ANSICHAR* TagComment);

private:
	static FBH_GameplayTags GameplayTags;
};
