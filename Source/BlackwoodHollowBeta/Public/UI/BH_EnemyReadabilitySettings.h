// Blackwood Hollow - enemy readability tuning (Phase 13D, #50)
// Target: Unreal Engine 5.8 (C++)
//
// Project Settings > Game > "Blackwood Hollow Enemy Readability" (saved to DefaultGame.ini). Values are recorded in claude/balance.md.
// Presentation only: rank and archetype come from the 12G data (BH_EnemyRank.h); nothing here changes an enemy.
//
// Colours are NOT stored here. Every colour is a NAME of a variable in the 13A palette asset (DA_UI_Palette); the library reads it by
// reflection, so the palette stays the single source of truth.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Combat/BH_EnemyRank.h"
#include "BH_EnemyReadabilitySettings.generated.h"

class USoundBase;
class UTexture2D;

UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Blackwood Hollow Enemy Readability"))
class BLACKWOODHOLLOWBETA_API UBH_EnemyReadabilitySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UBH_EnemyReadabilitySettings();

	static const UBH_EnemyReadabilitySettings* Get() { return GetDefault<UBH_EnemyReadabilitySettings>(); }

	/** The 13A palette data asset (DA_UI_Palette). Colour tokens below are variable names in it (case-insensitive). */
	UPROPERTY(Config, EditAnywhere, Category = "Palette", meta = (AllowedClasses = "/Script/Engine.DataAsset"))
	TSoftObjectPtr<UObject> PaletteAsset;

	// -- Level colour on the target panel (enemy level minus the local player's level) --------------

	/** Difference at or below this is Grey (well below the player). Style guide: 5 or more below. */
	UPROPERTY(Config, EditAnywhere, Category = "Level Colour")
	int32 GreyMaxDiff = -5;

	/** Difference at or below this (and above GreyMaxDiff) is White. Style guide: within +/-2. 3 to 4 below also reads White. */
	UPROPERTY(Config, EditAnywhere, Category = "Level Colour")
	int32 WhiteMaxDiff = 2;

	/** Difference at or below this (and above WhiteMaxDiff) is Orange (3 to 4 above); anything higher is Red (5+ above). */
	UPROPERTY(Config, EditAnywhere, Category = "Level Colour")
	int32 OrangeMaxDiff = 4;

	UPROPERTY(Config, EditAnywhere, Category = "Level Colour")
	FName LevelGreyToken = TEXT("LevelGrey");

	UPROPERTY(Config, EditAnywhere, Category = "Level Colour")
	FName LevelWhiteToken = TEXT("LevelWhite");

	UPROPERTY(Config, EditAnywhere, Category = "Level Colour")
	FName LevelOrangeToken = TEXT("LevelOrange");

	UPROPERTY(Config, EditAnywhere, Category = "Level Colour")
	FName LevelRedToken = TEXT("LevelRed");

	// -- Over-head bars ----------------------------------------------------------------------------

	/** Show the over-head bar on any enemy that has an aggro target (Common, Veteran and Elite). */
	UPROPERTY(Config, EditAnywhere, Category = "Overhead")
	bool bOverheadWhenAggroed = true;

	/** Show the over-head bar on any enemy below full health. */
	UPROPERTY(Config, EditAnywhere, Category = "Overhead")
	bool bOverheadWhenHurt = true;

	// -- Aggro sound (per rank; Common has none) -----------------------------------------------------

	/** Sound played when a Veteran, Elite or Boss starts fighting the LOCAL player. Veteran plays at the enemy, Elite and Boss in 2D. */
	UPROPERTY(Config, EditAnywhere, Category = "Aggro Sound")
	TMap<EBH_EnemyRank, TSoftObjectPtr<USoundBase>> AggroSounds;

	UPROPERTY(Config, EditAnywhere, Category = "Aggro Sound", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float AggroSoundVolume = 1.f;

	/** The same enemy does not cue again within this many seconds (it can drop and regain aggro). */
	UPROPERTY(Config, EditAnywhere, Category = "Aggro Sound", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float AggroCueCooldown = 12.f;

	/** Aggro cues further than this (cm) from the local pawn are skipped. 0 = no limit. */
	UPROPERTY(Config, EditAnywhere, Category = "Aggro Sound", meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float AggroCueMaxDistance = 4000.f;

	// -- Archetype language -----------------------------------------------------------------------

	/** Role glyph on the lock-on target panel, per archetype (icons from the already-imported icon packs). */
	UPROPERTY(Config, EditAnywhere, Category = "Archetype")
	TMap<EBH_EnemyArchetype, TSoftObjectPtr<UTexture2D>> RoleGlyphs;

	/** Palette variable name for each archetype's telegraph colour (one colour per archetype, always). */
	UPROPERTY(Config, EditAnywhere, Category = "Archetype")
	TMap<EBH_EnemyArchetype, FName> TelegraphColorTokens;

	/** Optional sound played at the start of an enemy telegraph, per archetype (one sound per archetype, always). Empty = silent. */
	UPROPERTY(Config, EditAnywhere, Category = "Archetype")
	TMap<EBH_EnemyArchetype, TSoftObjectPtr<USoundBase>> TelegraphSounds;

	UPROPERTY(Config, EditAnywhere, Category = "Archetype", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float TelegraphSoundVolume = 0.8f;
};
