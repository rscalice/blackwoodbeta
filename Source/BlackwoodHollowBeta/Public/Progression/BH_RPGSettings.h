// Blackwood Hollow - Phase 9 RPG scaling settings
// Target: Unreal Engine 5.8 (C++)
//
// Project Settings > Game > "Blackwood Hollow RPG" (saved to DefaultGame.ini). Holds the two scaling Curve Tables, the level cap,
// the XP share radius and the armor weight class tuning.
//
// CURVE TABLE ROWS (X = level, Y = value; both tables are optional: a missing table or row logs ONE warning and the caller keeps
// its current values):
//   Player table : MaxHealth, MaxPosture, MaxStamina, XPToNextLevel (XP needed to go from level L to L+1)
//   Enemy table  : <Prefix>.MaxHealth, <Prefix>.MaxPosture, <Prefix>.AttackPower, <Prefix>.Defense, <Prefix>.XPReward
//                  (<Prefix> = ABH_EnemyBase::ScalingRowPrefix, e.g. "Crab")

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/CurveTable.h"
#include "Items/BH_EquipmentTypes.h"
#include "Items/BH_ArmorVisualTypes.h"
#include "BH_RPGSettings.generated.h"

class UBH_FootstepSet;

/** Curve table row names (plain constexpr literals: no static-init-order globals). */
namespace BH_ScalingRows
{
	inline constexpr const TCHAR* MaxHealth = TEXT("MaxHealth");
	inline constexpr const TCHAR* MaxPosture = TEXT("MaxPosture");
	inline constexpr const TCHAR* MaxStamina = TEXT("MaxStamina");
	inline constexpr const TCHAR* XPToNextLevel = TEXT("XPToNextLevel");
	inline constexpr const TCHAR* AttackPower = TEXT("AttackPower");
	inline constexpr const TCHAR* Defense = TEXT("Defense");
	inline constexpr const TCHAR* XPReward = TEXT("XPReward");
}

UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Blackwood Hollow RPG"))
class BLACKWOODHOLLOWBETA_API UBH_RPGSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UBH_RPGSettings();

	/** The settings object (class default object), never null. */
	static const UBH_RPGSettings* Get();

	/** Level cap (>= 1) from the settings. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|RPG")
	static int32 GetMaxLevel();

	// -- Scaling tables -----------------------------------------------------------------------

	/** Player rows: MaxHealth, MaxPosture, MaxStamina, XPToNextLevel. */
	UPROPERTY(Config, EditAnywhere, Category = "Scaling")
	TSoftObjectPtr<UCurveTable> PlayerScalingTable;

	/** Enemy rows: <Prefix>.MaxHealth, <Prefix>.MaxPosture, <Prefix>.AttackPower, <Prefix>.Defense, <Prefix>.XPReward. */
	UPROPERTY(Config, EditAnywhere, Category = "Scaling")
	TSoftObjectPtr<UCurveTable> EnemyScalingTable;

	UPROPERTY(Config, EditAnywhere, Category = "Scaling", meta = (ClampMin = "1"))
	int32 MaxLevel = 50;

	/** Used for XPToNextLevel when the player table (or its row) is missing: XP to next = this * current level. */
	UPROPERTY(Config, EditAnywhere, Category = "Scaling", meta = (ClampMin = "1"))
	int32 FallbackXPPerLevel = 100;

	// -- XP ---------------------------------------------------------------------------------------

	/** A kill's XP goes to every living player pawn within this distance (cm) of the dead enemy. */
	UPROPERTY(Config, EditAnywhere, Category = "XP", meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float XPShareRadius = 5000.f;

	// -- Armor weight class (the heaviest equipped piece decides) --------------------------------

	UPROPERTY(Config, EditAnywhere, Category = "Armor Weight|Light", meta = (ClampMin = "0.05"))
	float LightStaminaRegenMultiplier = 1.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Armor Weight|Light", meta = (ClampMin = "0.05"))
	float LightDodgeDistanceMultiplier = 1.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Armor Weight|Medium", meta = (ClampMin = "0.05"))
	float MediumStaminaRegenMultiplier = 0.85f;

	UPROPERTY(Config, EditAnywhere, Category = "Armor Weight|Medium", meta = (ClampMin = "0.05"))
	float MediumDodgeDistanceMultiplier = 0.95f;

	UPROPERTY(Config, EditAnywhere, Category = "Armor Weight|Heavy", meta = (ClampMin = "0.05"))
	float HeavyStaminaRegenMultiplier = 0.65f;

	UPROPERTY(Config, EditAnywhere, Category = "Armor Weight|Heavy", meta = (ClampMin = "0.05"))
	float HeavyDodgeDistanceMultiplier = 0.85f;

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|RPG")
	float GetStaminaRegenMultiplier(EBH_ArmorWeightClass WeightClass) const;

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|RPG")
	float GetDodgeDistanceMultiplier(EBH_ArmorWeightClass WeightClass) const;

	// -- Armor visuals (Phase 11E) ----------------------------------------------------------------

	/**
	 * Cosmetic starting outfit: shown in any slot with no armor equipped (or whose equipped piece has no visual). Head is empty on
	 * purpose (no helm). Defaults: the BasicCloth MaleNormal chest / gloves / pants / boots.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Armor Visuals")
	TArray<FBH_StartingOutfitPiece> StartingOutfit;

	/** Sets the bh.Armor.Give <Light|Medium|Heavy> debug command grants and equips (item Blueprint classes, one per slot). Debug builds only. */
	UPROPERTY(Config, EditAnywhere, Category = "Armor Visuals|Debug")
	TArray<FBH_ArmorDebugSet> DebugArmorSets;

	/** First starting-outfit visual with a mesh for Slot, or nullptr. */
	const FBH_ArmorVisual* FindStartingOutfitVisual(EBH_EquipSlot Slot) const;

	/** The debug set called SetName (case-insensitive), or nullptr. */
	const FBH_ArmorDebugSet* FindDebugArmorSet(const FString& SetName) const;

	// -- Lookups ------------------------------------------------------------------------------

	/**
	 * Evaluates RowName of Table at Level. @return false (OutValue untouched) when the table is unset / not loadable or has no such row;
	 * the first failure per table / row logs one warning, later ones are silent.
	 */
	static bool EvaluateCurveRow(const TSoftObjectPtr<UCurveTable>& Table, FName RowName, float Level, float& OutValue);

	/** Player table row (MaxHealth, ...) at Level. */
	bool GetPlayerStat(FName RowName, int32 Level, float& OutValue) const;

	/** Enemy table row "<Prefix>.<Stat>" at Level. A None prefix always returns false. */
	bool GetEnemyStat(FName Prefix, FName Stat, int32 Level, float& OutValue) const;

	/** XP needed to go from Level to Level + 1 (player table row XPToNextLevel, else FallbackXPPerLevel * Level). 0 at or above the cap. */
	int32 GetXPToNextLevel(int32 Level) const;

	// -- Audio (Phase 10A) ----------------------------------------------------------------------

	/** Footstep sounds used by UBH_AN_Footstep when the notify has no FootstepSet override (e.g. /Game/BlackwoodHollow/Audio/Footsteps/DA_FootstepSet). */
	UPROPERTY(Config, EditAnywhere, Category = "Audio")
	TSoftObjectPtr<UBH_FootstepSet> DefaultFootstepSet;

	/** DefaultFootstepSet, loaded synchronously on first use (null if unset or not loadable). */
	UBH_FootstepSet* GetDefaultFootstepSet() const;
};
