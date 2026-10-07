// Blackwood Hollow - Shield Bash (implementation)

#include "AbilitySystem/Abilities/AH_GA_ShieldBash.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystem/Effects/AH_GE_CombatEffects.h"

UAH_GA_ShieldBash::UAH_GA_ShieldBash()
{
	FGameplayTagContainer BashAssetTags;
	BashAssetTags.AddTag(TAG_Ability_Combat_MeleeAttack); // parry cancels it like any melee attack
	BashAssetTags.AddTag(TAG_Ability_Combat_ShieldBash);
	SetAssetTags(BashAssetTags);

	ComboSectionNames = { FName(TEXT("Bash")) };
	BaseDamage = 2.f;
	bAddAttackPower = false;
	BasePostureDamage = 35.f;
	StaminaCost = 18.f;
	bIgnoreBlockForPosture = true;
	HitCueTag = TAG_GameplayCue_Combat_Hit_ShieldBash; // heavier, metallic impact (GC_Combat_ShieldBashHit)

	CooldownGameplayEffectClass = UAH_GE_ShieldBashCooldown::StaticClass();
}
