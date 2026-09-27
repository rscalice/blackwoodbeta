// Blackwood Hollow - Native Gameplay Tag hierarchy (implementation)

#include "AbilitySystem/BH_GameplayTags.h"
#include "GameplayTagsManager.h"

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
UE_DEFINE_GAMEPLAY_TAG(TAG_State_Combat_ComboWindow, "State.Combat.ComboWindow");

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

	// Event.Combat
	AddTag(Event_Combat_Hit, "Event.Combat.Hit", "Sent to the victim by UANS_MeleeHitbox when a melee sweep connects, BEFORE damage is applied (parry hooks here).");
	AddTag(Event_Combat_PostureBreak, "Event.Combat.PostureBreak", "Sent the instant Posture is broken.");
	AddTag(Event_Combat_Death, "Event.Combat.Death", "Sent when Health reaches zero.");
	AddTag(Event_Combat_BlightDamage, "Event.Combat.BlightDamage", "Sent when Blight damage is applied to an actor.");
	AddTag(Event_Combat_BlightShieldDepleted, "Event.Combat.BlightShieldDepleted", "Sent when the Heart-Fragment's Blight shield hits zero.");
	AddTag(Event_Combat_OverloadBurst, "Event.Combat.OverloadBurst", "Sent to activate GA_HeartFragment_OverloadBurst.");
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
}
