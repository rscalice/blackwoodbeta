// Blackwood Hollow - Native Gameplay Tag hierarchy (implementation)

#include "AbilitySystem/BH_GameplayTags.h"
#include "GameplayTagsManager.h"

DEFINE_LOG_CATEGORY(LogBHCombat);

FBH_GameplayTags FBH_GameplayTags::GameplayTags;

// ---------------------------------------------------------------------------
// UE_DEFINE_GAMEPLAY_TAG registers each tag with the GameplayTags manager at
// static-init time and pairs with the UE_DECLARE_GAMEPLAY_TAG_EXTERN in the
// header. These are usable directly (e.g. TAG_State_Combat_InCombat) without
// going through FBH_GameplayTags::Get(), which the struct below also fills
// in for readability/parity with Lyra-derived codebases.
// ---------------------------------------------------------------------------

// State.Combat.*
UE_DEFINE_GAMEPLAY_TAG(TAG_State_Combat_InCombat, "State.Combat.InCombat");
UE_DEFINE_GAMEPLAY_TAG(TAG_State_Combat_Blocking, "State.Combat.Blocking");
UE_DEFINE_GAMEPLAY_TAG(TAG_State_Combat_Parrying, "State.Combat.Parrying");
UE_DEFINE_GAMEPLAY_TAG(TAG_State_Combat_Staggered, "State.Combat.Staggered");
UE_DEFINE_GAMEPLAY_TAG(TAG_State_Combat_PostureBroken, "State.Combat.PostureBroken");
UE_DEFINE_GAMEPLAY_TAG(TAG_State_Combat_BlightShielded, "State.Combat.BlightShielded");
UE_DEFINE_GAMEPLAY_TAG(TAG_State_Combat_Overloading, "State.Combat.Overloading");
UE_DEFINE_GAMEPLAY_TAG(TAG_State_Combat_Dead, "State.Combat.Dead");
UE_DEFINE_GAMEPLAY_TAG(TAG_State_Combat_Attacking, "State.Combat.Attacking");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_State_Combat_HyperArmor, "State.Combat.HyperArmor", "Hit reactions cannot stagger this actor (heavy weapon swing); damage still applies.");
UE_DEFINE_GAMEPLAY_TAG(TAG_State_Combat_ComboWindow, "State.Combat.ComboWindow");
UE_DEFINE_GAMEPLAY_TAG(TAG_State_Combat_PostureRegenDelayed, "State.Combat.PostureRegenDelayed");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_State_Combat_MovementLocked, "State.Combat.MovementLocked", "Movement input is ignored (full-body attack); root motion still moves the capsule.");

// Event.Combat.*
UE_DEFINE_GAMEPLAY_TAG(TAG_Event_Combat_Hit, "Event.Combat.Hit");
UE_DEFINE_GAMEPLAY_TAG(TAG_Event_Combat_PostureBreak, "Event.Combat.PostureBreak");
UE_DEFINE_GAMEPLAY_TAG(TAG_Event_Combat_Death, "Event.Combat.Death");
UE_DEFINE_GAMEPLAY_TAG(TAG_Event_Combat_BlightDamage, "Event.Combat.BlightDamage");
UE_DEFINE_GAMEPLAY_TAG(TAG_Event_Combat_BlightShieldDepleted, "Event.Combat.BlightShieldDepleted");
UE_DEFINE_GAMEPLAY_TAG(TAG_Event_Combat_OverloadBurst, "Event.Combat.OverloadBurst");
UE_DEFINE_GAMEPLAY_TAG(TAG_Event_Combat_OverloadBurst_Ready, "Event.Combat.OverloadBurst.Ready");
UE_DEFINE_GAMEPLAY_TAG(TAG_Event_Combat_DamageReceived, "Event.Combat.DamageReceived");
UE_DEFINE_GAMEPLAY_TAG(TAG_Event_Combat_HitDealt, "Event.Combat.HitDealt");
UE_DEFINE_GAMEPLAY_TAG(TAG_Event_Combat_ComboWindow_Open, "Event.Combat.ComboWindow.Open");
UE_DEFINE_GAMEPLAY_TAG(TAG_Event_Combat_ComboWindow_Close, "Event.Combat.ComboWindow.Close");
UE_DEFINE_GAMEPLAY_TAG(TAG_Event_Combat_Input_Attack, "Event.Combat.Input.Attack");
UE_DEFINE_GAMEPLAY_TAG(TAG_Event_Combat_Parry_Success, "Event.Combat.Parry.Success");

// Ability.* / Damage.* / Data.*
UE_DEFINE_GAMEPLAY_TAG(TAG_Ability_Combat_MeleeAttack, "Ability.Combat.MeleeAttack");
UE_DEFINE_GAMEPLAY_TAG(TAG_Ability_Combat_Parry, "Ability.Combat.Parry");
UE_DEFINE_GAMEPLAY_TAG(TAG_Damage_Type_Melee, "Damage.Type.Melee");
UE_DEFINE_GAMEPLAY_TAG(TAG_Data_Damage, "Data.Damage");
UE_DEFINE_GAMEPLAY_TAG(TAG_Data_PostureDamage, "Data.PostureDamage");
UE_DEFINE_GAMEPLAY_TAG(TAG_Event_Combat_BlockImpact, "Event.Combat.BlockImpact");
UE_DEFINE_GAMEPLAY_TAG(TAG_Ability_Combat_Block, "Ability.Combat.Block");
UE_DEFINE_GAMEPLAY_TAG(TAG_Ability_Combat_HitReaction, "Ability.Combat.HitReaction");
UE_DEFINE_GAMEPLAY_TAG(TAG_Ability_Combat_PostureBreak, "Ability.Combat.PostureBreak");

// Phase 6
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_State_Combat_Flurry, "State.Combat.Flurry", "Dual-sword momentum: AttackSpeed stacks from UAH_GE_Flurry are active.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_State_Combat_RiposteReady, "State.Combat.RiposteReady", "A perfect parry just landed: the next melee hit deals RiposteDamageMultiplier damage.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Ability_Combat_ShieldBash, "Ability.Combat.ShieldBash", "Identifies the shield bash (guard-breaker) ability.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Cooldown_Combat_ShieldBash, "Cooldown.Combat.ShieldBash", "Shield bash is on cooldown.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_GameplayCue_Combat_Hit, "GameplayCue.Combat.Hit", "Cosmetic cue: a melee hit connected (hit-stop + camera shake).");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_GameplayCue_Combat_ParrySuccess, "GameplayCue.Combat.ParrySuccess", "Cosmetic cue: a parry deflected a hit (heavy hit-stop + camera punch).");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_GameplayCue_Combat_PostureBroken, "GameplayCue.Combat.PostureBroken", "Cosmetic cue: a target's posture broke (shatter VFX/SFX).");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_GameplayCue_Combat_Hit_ShieldBash, "GameplayCue.Combat.Hit.ShieldBash", "Cosmetic cue: a shield bash connected (heavier, metallic variant of the Hit cue).");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Combat_HitResult_Blocked, "Combat.HitResult.Blocked", "Flag carried in FGameplayCueParameters::AggregatedSourceTags: the victim blocked this hit.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Combat_HitResult_Fatal, "Combat.HitResult.Fatal", "Flag carried in FGameplayCueParameters::AggregatedSourceTags: the victim's Health was zero after this hit's damage (set on the server, so every machine agrees).");

// Phase 7A
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_State_Combat_Dodging, "State.Combat.Dodging", "A dodge ability is active (granted by UAH_GA_Dodge).");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_State_Combat_Invulnerable, "State.Combat.Invulnerable", "Dodge i-frames: melee damage / posture damage and melee hits are ignored.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_State_Combat_StaminaRegenDelayed, "State.Combat.StaminaRegenDelayed", "Stamina was just spent: passive stamina regen is paused for bh.Combat.StaminaRegenDelay seconds (loose tag).");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Ability_Combat_Dodge, "Ability.Combat.Dodge", "Identifies the dodge ability.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Data_StaminaCost, "Data.StaminaCost", "SetByCaller key: stamina cost (positive number) read by UAH_GE_StaminaCost.");

// Phase 7C
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Data_Cooldown, "Data.Cooldown", "SetByCaller key: cooldown duration in seconds, read by UAH_GE_Cooldown_Base.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Cooldown_Fragment_OverloadBurst, "Cooldown.Fragment.OverloadBurst", "Heart-Fragment Overload Burst is on cooldown (granted dynamically by UAH_GA_FragmentBase::ApplyCooldown).");

// Phase 8C
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Data_Equip_AttackPower, "Data.Equip.AttackPower", "SetByCaller key: additive AttackPower bonus read by UAH_GE_EquipmentStatMod.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Data_Equip_Defense, "Data.Equip.Defense", "SetByCaller key: additive Defense bonus read by UAH_GE_EquipmentStatMod.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Data_Equip_MaxStamina, "Data.Equip.MaxStamina", "SetByCaller key: additive MaxStamina bonus read by UAH_GE_EquipmentStatMod.");

// Phase 9: RPG scaling
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Data_DamageMultiplier, "Data.DamageMultiplier", "SetByCaller key: total damage multiplier (step * hitbox * identity * riposte), read by UAH_ExecCalc_Damage. Default 1.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Data_AttackPowerScale, "Data.AttackPowerScale", "SetByCaller key: how much of the source's AttackPower is added to the base damage, read by UAH_ExecCalc_Damage. Default 1; shield bash passes 0.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Data_Equip_StaminaRegenMult, "Data.Equip.StaminaRegenMult", "SetByCaller key: multiplier on StaminaRegenRate read by UAH_GE_ArmorWeight (heaviest equipped armor class).");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_State_Armor_Weight_Medium, "State.Armor.Weight.Medium", "Heaviest equipped armor piece is Medium (granted by UAH_GE_ArmorWeight_Medium).");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_State_Armor_Weight_Heavy, "State.Armor.Weight.Heavy", "Heaviest equipped armor piece is Heavy (granted by UAH_GE_ArmorWeight_Heavy).");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_GameplayCue_Player_LevelUp, "GameplayCue.Player.LevelUp", "Cosmetic cue: a player gained a level (server-executed on the pawn ASC by UBH_ProgressionComponent; Instigator = pawn, Location = pawn location, RawMagnitude = new level).");

// Phase 3: weapon stance
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Stance_Weapon, "Stance.Weapon", "Parent of the weapon stance tags (UBH_StanceComponent).");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Stance_Weapon_Unarmed, "Stance.Weapon.Unarmed", "No weapons drawn.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Stance_Weapon_Greatsword, "Stance.Weapon.Greatsword", "Two-handed greatsword stance.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Stance_Weapon_SwordShield, "Stance.Weapon.SwordShield", "Sword and shield stance (legacy key SwordAndShield).");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Stance_Weapon_DualSword, "Stance.Weapon.DualSword", "Dual sword stance.");

// Phase 8D placeholder stances
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Stance_Weapon_OneHandedSword, "Stance.Weapon.OneHandedSword", "Placeholder: one-handed sword stance (not implemented yet).");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Stance_Weapon_Bow, "Stance.Weapon.Bow", "Placeholder: bow stance (not implemented yet).");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Stance_Weapon_Crossbow, "Stance.Weapon.Crossbow", "Placeholder: crossbow stance (not implemented yet).");

// Phase 8B: Heart-Fragment slot categories
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Fragment_Category_Vitality, "Fragment.Category.Vitality", "Heart-Fragment category: healing / survivability.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Fragment_Category_Offensive, "Fragment.Category.Offensive", "Heart-Fragment category: damage / burst.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Fragment_Category_BlightResist, "Fragment.Category.BlightResist", "Heart-Fragment category: Blight protection.");

// Phase 8C: Blight DoT
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_State_Status_Blighted, "State.Status.Blighted", "Blight damage-over-time is ticking on this actor (UAH_GE_BlightDoT).");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Data_Blight_DPS, "Data.Blight.DPS", "SetByCaller key: Blight damage per second, read by UAH_MMC_BlightDoT.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Damage_Type_Blight, "Damage.Type.Blight", "Damage spec came from Blight DoT: not blockable, no hit reaction / posture / hit-stop.");

// Weapon drawn / sheathed
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_State_Weapon_Drawn, "State.Weapon.Drawn", "Weapon is drawn: combat locomotion / hold pose (UBH_StanceComponent).");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_State_Weapon_Sheathed, "State.Weapon.Sheathed", "Weapon is sheathed / relaxed (UBH_StanceComponent).");

namespace BH_Stance
{
	FGameplayTag FromLegacyName(FName LegacyName)
	{
		const FString Name = LegacyName.ToString();
		if (Name.Equals(TEXT("Unarmed"), ESearchCase::IgnoreCase)) { return TAG_Stance_Weapon_Unarmed; }
		if (Name.Equals(TEXT("Greatsword"), ESearchCase::IgnoreCase)) { return TAG_Stance_Weapon_Greatsword; }
		if (Name.Equals(TEXT("DualSword"), ESearchCase::IgnoreCase)) { return TAG_Stance_Weapon_DualSword; }
		if (Name.Equals(TEXT("OneHandedSword"), ESearchCase::IgnoreCase)) { return TAG_Stance_Weapon_OneHandedSword; }
		if (Name.Equals(TEXT("Bow"), ESearchCase::IgnoreCase)) { return TAG_Stance_Weapon_Bow; }
		if (Name.Equals(TEXT("Crossbow"), ESearchCase::IgnoreCase)) { return TAG_Stance_Weapon_Crossbow; }
		if (Name.Equals(TEXT("SwordAndShield"), ESearchCase::IgnoreCase)
			|| Name.Equals(TEXT("SwordShield"), ESearchCase::IgnoreCase)
			|| Name.Equals(TEXT("Sword&Shield"), ESearchCase::IgnoreCase))
		{
			return TAG_Stance_Weapon_SwordShield;
		}
		return FGameplayTag();
	}

	FName ToLegacyName(FGameplayTag StanceTag)
	{
		if (StanceTag == TAG_Stance_Weapon_Unarmed.GetTag()) { return FName(TEXT("Unarmed")); }
		if (StanceTag == TAG_Stance_Weapon_Greatsword.GetTag()) { return FName(TEXT("Greatsword")); }
		if (StanceTag == TAG_Stance_Weapon_SwordShield.GetTag()) { return FName(TEXT("SwordAndShield")); }
		if (StanceTag == TAG_Stance_Weapon_DualSword.GetTag()) { return FName(TEXT("DualSword")); }
		if (StanceTag == TAG_Stance_Weapon_OneHandedSword.GetTag()) { return FName(TEXT("OneHandedSword")); }
		if (StanceTag == TAG_Stance_Weapon_Bow.GetTag()) { return FName(TEXT("Bow")); }
		if (StanceTag == TAG_Stance_Weapon_Crossbow.GetTag()) { return FName(TEXT("Crossbow")); }
		return NAME_None;
	}

	bool IsPlaceholderStance(FGameplayTag Tag)
	{
		return Tag.IsValid()
			&& (Tag == TAG_Stance_Weapon_OneHandedSword.GetTag()
				|| Tag == TAG_Stance_Weapon_Bow.GetTag()
				|| Tag == TAG_Stance_Weapon_Crossbow.GetTag());
	}

	bool IsWeaponStance(FGameplayTag Tag)
	{
		return Tag.IsValid() && Tag != TAG_Stance_Weapon.GetTag() && Tag.MatchesTag(TAG_Stance_Weapon.GetTag());
	}

	const TArray<FGameplayTag>& AllWeaponStances()
	{
		static const TArray<FGameplayTag> Stances = {
			TAG_Stance_Weapon_Unarmed.GetTag(), TAG_Stance_Weapon_Greatsword.GetTag(),
			TAG_Stance_Weapon_SwordShield.GetTag(), TAG_Stance_Weapon_DualSword.GetTag() };
		return Stances;
	}
}

void FBH_GameplayTags::InitializeNativeTags()
{
	GameplayTags.AddAllTags();
}

void FBH_GameplayTags::AddTag(FGameplayTag& OutTag, const ANSICHAR* TagName, const ANSICHAR* TagComment)
{
	OutTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TagName), FString(TEXT("(Native) ")) + FString(TagComment));
}

void FBH_GameplayTags::AddAllTags()
{
	// State.Combat
	AddTag(State_Combat_InCombat, "State.Combat.InCombat", "Actor is actively engaged in combat.");
	AddTag(State_Combat_Blocking, "State.Combat.Blocking", "Actor is holding a block.");
	AddTag(State_Combat_Parrying, "State.Combat.Parrying", "Actor is within an active parry window.");
	AddTag(State_Combat_Staggered, "State.Combat.Staggered", "Actor is staggered and cannot act.");
	AddTag(State_Combat_PostureBroken, "State.Combat.PostureBroken", "Actor's Posture has been broken, opening a punish window.");
	AddTag(State_Combat_BlightShielded, "State.Combat.BlightShielded", "Heart-Fragment Blight shield is currently absorbing damage.");
	AddTag(State_Combat_Overloading, "State.Combat.Overloading", "Heart-Fragment Overload Burst ability is mid-activation.");
	AddTag(State_Combat_Dead, "State.Combat.Dead", "Actor has died.");
	AddTag(State_Combat_Attacking, "State.Combat.Attacking", "A melee attack ability is active.");
	AddTag(State_Combat_ComboWindow, "State.Combat.ComboWindow", "Inside a UANS_ComboWindow: combo input will be accepted.");
	AddTag(State_Combat_PostureRegenDelayed, "State.Combat.PostureRegenDelayed", "Posture was just damaged: passive posture regen is paused for bh.Combat.PostureRegenDelay seconds (server-only loose tag).");

	// Event.Combat
	AddTag(Event_Combat_Hit, "Event.Combat.Hit", "Sent to the victim by UANS_MeleeHitbox when a melee sweep connects, BEFORE damage is applied (parry hooks here).");
	AddTag(Event_Combat_PostureBreak, "Event.Combat.PostureBreak", "Sent the instant Posture is broken.");
	AddTag(Event_Combat_Death, "Event.Combat.Death", "Sent when Health reaches zero.");
	AddTag(Event_Combat_BlightDamage, "Event.Combat.BlightDamage", "Sent when Blight damage is applied to an actor.");
	AddTag(Event_Combat_BlightShieldDepleted, "Event.Combat.BlightShieldDepleted", "Sent when the Heart-Fragment's Blight shield hits zero.");
	AddTag(Event_Combat_OverloadBurst, "Event.Combat.OverloadBurst", "Sent by UAH_GA_OverloadBurst when it activates (BP_BlightVolume listens). Does NOT activate the ability.");
	AddTag(Event_Combat_OverloadBurst_Ready, "Event.Combat.OverloadBurst.Ready", "Sent when the Overload Burst comes off cooldown / refills.");
	AddTag(Event_Combat_DamageReceived, "Event.Combat.DamageReceived", "Sent to the victim after IncomingDamage has been applied to Health.");
	AddTag(Event_Combat_HitDealt, "Event.Combat.HitDealt", "Sent to the attacker by UANS_MeleeHitbox when its sweep connects.");
	AddTag(Event_Combat_ComboWindow_Open, "Event.Combat.ComboWindow.Open", "UANS_ComboWindow began: combo input may advance the attack.");
	AddTag(Event_Combat_ComboWindow_Close, "Event.Combat.ComboWindow.Close", "UANS_ComboWindow ended.");
	AddTag(Event_Combat_Input_Attack, "Event.Combat.Input.Attack", "Attack input pressed while a melee ability is already running (buffered combo input).");
	AddTag(Event_Combat_Parry_Success, "Event.Combat.Parry.Success", "Sent to both parrier and attacker when a hit lands inside an active parry window.");

	// Ability / Damage / Data
	AddTag(Ability_Combat_MeleeAttack, "Ability.Combat.MeleeAttack", "Identifies melee attack abilities.");
	AddTag(Ability_Combat_Parry, "Ability.Combat.Parry", "Identifies the parry ability.");
	AddTag(Damage_Type_Melee, "Damage.Type.Melee", "Damage spec came from a melee hit (parryable).");
	AddTag(Data_Damage, "Data.Damage", "SetByCaller key: damage routed into IncomingDamage.");
	AddTag(Data_PostureDamage, "Data.PostureDamage", "SetByCaller key: posture delta (negative = damage).");
	AddTag(Event_Combat_BlockImpact, "Event.Combat.BlockImpact", "Sent to the blocker (instead of DamageReceived) when a hit is blocked. EventMagnitude = posture cost.");
	AddTag(Ability_Combat_Block, "Ability.Combat.Block", "Identifies the hold-to-block ability.");
	AddTag(Ability_Combat_HitReaction, "Ability.Combat.HitReaction", "Identifies the hit reaction ability.");
	AddTag(Ability_Combat_PostureBreak, "Ability.Combat.PostureBreak", "Identifies the posture break ability.");

	// Phase 7C
	AddTag(Data_Cooldown, "Data.Cooldown", "SetByCaller key: cooldown duration in seconds.");
	AddTag(Cooldown_Fragment_OverloadBurst, "Cooldown.Fragment.OverloadBurst", "Heart-Fragment Overload Burst is on cooldown.");

	// Phase 3: Stance.Weapon
	AddTag(Stance_Weapon_Unarmed, "Stance.Weapon.Unarmed", "No weapons drawn.");
	AddTag(Stance_Weapon_Greatsword, "Stance.Weapon.Greatsword", "Two-handed greatsword stance.");
	AddTag(Stance_Weapon_SwordShield, "Stance.Weapon.SwordShield", "Sword and shield stance.");
	AddTag(Stance_Weapon_DualSword, "Stance.Weapon.DualSword", "Dual sword stance.");

	// Weapon state
	AddTag(State_Weapon_Drawn, "State.Weapon.Drawn", "Weapon is drawn (combat locomotion).");
	AddTag(State_Weapon_Sheathed, "State.Weapon.Sheathed", "Weapon is sheathed (relaxed locomotion).");
}
