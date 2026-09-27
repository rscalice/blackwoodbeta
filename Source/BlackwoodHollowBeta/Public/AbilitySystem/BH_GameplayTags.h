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

// ---------------------------------------------------------------------------
// Weapon/combat STANCE is not duplicated here as gameplay tags. GASP's own
// replicated OverlayPose enum (/GASPALS/OverlaySystem/Blueprints/Enum_OverlayPose)
// is the single source of truth for "what's equipped" -- see
// UBH_CombatFunctionLibrary::ApplyOverlayPoseByDisplayName(). Keeping a
// parallel Stance.* tag hierarchy in sync with that enum by hand was the
// wrong call; if ability activation ever needs to gate on weapon stance,
// prefer reading OverlayPose directly (or mirror the *current* pose into a
// single informational tag at the point it changes) rather than reviving a
// hand-maintained tag-to-enum mapping table.
// ---------------------------------------------------------------------------
// State.Combat.*  -- persistent/loose tags describing current combat state
// ---------------------------------------------------------------------------
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Combat_InCombat);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Combat_Blocking);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Combat_Parrying);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Combat_Staggered);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Combat_PostureBroken);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Combat_BlightShielded);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Combat_Overloading);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Combat_Dead);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Combat_Attacking);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Combat_ComboWindow);

// ---------------------------------------------------------------------------
// Event.Combat.*  -- momentary gameplay events sent via SendGameplayEventToActor
// ---------------------------------------------------------------------------
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Event_Combat_Hit);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Event_Combat_PostureBreak);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Event_Combat_Death);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Event_Combat_BlightDamage);
BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Event_Combat_BlightShieldDepleted);
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
	FGameplayTag State_Combat_BlightShielded;
	FGameplayTag State_Combat_Overloading;
	FGameplayTag State_Combat_Dead;
	FGameplayTag State_Combat_Attacking;
	FGameplayTag State_Combat_ComboWindow;

	// Event.Combat
	FGameplayTag Event_Combat_Hit;
	FGameplayTag Event_Combat_PostureBreak;
	FGameplayTag Event_Combat_Death;
	FGameplayTag Event_Combat_BlightDamage;
	FGameplayTag Event_Combat_BlightShieldDepleted;
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

protected:
	void AddAllTags();
	void AddTag(FGameplayTag& OutTag, const ANSICHAR* TagName, const ANSICHAR* TagComment);

private:
	static FBH_GameplayTags GameplayTags;
};
