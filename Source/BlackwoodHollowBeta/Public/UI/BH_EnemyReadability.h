// Blackwood Hollow - enemy readability helpers (Phase 13D, #50)
// Target: Unreal Engine 5.8 (C++)
//
// One place that turns the 12G rank / archetype data into what the UI shows: the name, the rank title, the level colour tier against
// the viewing player, the role glyph, the aggro sound and the archetype telegraph colour. Presentation only; nothing here changes an
// enemy. Tuning lives in UBH_EnemyReadabilitySettings; colours are read by NAME from the 13A palette asset, never stored as raw values.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Combat/BH_EnemyRank.h"
#include "BH_EnemyReadability.generated.h"

class USoundBase;
class UTexture2D;

/** Enemy level against the viewer's level (13A level colours). */
UENUM(BlueprintType)
enum class EBH_LevelTier : uint8
{
	Grey,
	White,
	Orange,
	Red,
};

USTRUCT(BlueprintType)
struct FBH_EnemyReadability
{
	GENERATED_BODY()

	/** False when the actor has no rank data (not an enemy). */
	UPROPERTY(BlueprintReadOnly, Category = "Readability")
	bool bValid = false;

	UPROPERTY(BlueprintReadOnly, Category = "Readability")
	EBH_EnemyRank Rank = EBH_EnemyRank::Common;

	UPROPERTY(BlueprintReadOnly, Category = "Readability")
	EBH_EnemyArchetype Archetype = EBH_EnemyArchetype::Soldier;

	UPROPERTY(BlueprintReadOnly, Category = "Readability")
	int32 Level = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Readability")
	EBH_LevelTier LevelTier = EBH_LevelTier::White;

	/** The enemy's display name (identity / ABH_EnemyBase), or the actor name. */
	UPROPERTY(BlueprintReadOnly, Category = "Readability")
	FText Name;

	/** Rank title: empty for Common and Veteran, "Elite <role>" for Elite, "Boss" for Boss. No new enemy data: built from rank + archetype. */
	UPROPERTY(BlueprintReadOnly, Category = "Readability")
	FText Title;

	/** True when the top-centre boss bar presents this enemy (Boss rank, bIsBoss, or Elite with bEliteUsesBossBar). */
	UPROPERTY(BlueprintReadOnly, Category = "Readability")
	bool bBossPresented = false;
};

UCLASS()
class BLACKWOODHOLLOWBETA_API UBH_EnemyReadabilityLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Everything the UI needs about Enemy as seen by Viewer (the local pawn; may be null, then the tier is White). */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Enemy|Readability")
	static FBH_EnemyReadability Describe(const AActor* Enemy, const AActor* Viewer);

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Enemy|Readability")
	static EBH_LevelTier GetLevelTier(int32 EnemyLevel, int32 ViewerLevel);

	/** Level attribute of Viewer's ability system (1 when there is none). */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Enemy|Readability")
	static int32 GetViewerLevel(const AActor* Viewer);

	/** Palette colour for a level tier (13A level tokens). */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Enemy|Readability")
	static FLinearColor GetLevelColor(EBH_LevelTier Tier);

	/** The one telegraph colour of an archetype (palette token from the settings). */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Enemy|Readability")
	static FLinearColor GetArchetypeTelegraphColor(EBH_EnemyArchetype Archetype);

	/** A colour variable of the 13A palette asset by name (case-insensitive). White when the palette or the token is missing. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Enemy|Readability")
	static FLinearColor GetPaletteColor(FName Token);

	/** Role glyph texture of an archetype, or null when none is set. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Enemy|Readability")
	static UTexture2D* GetRoleGlyph(EBH_EnemyArchetype Archetype);

	/** Aggro sound of a rank (null for Common or when unset). */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Enemy|Readability")
	static USoundBase* GetAggroSound(EBH_EnemyRank Rank);

	/** Telegraph start sound of an archetype (null when unset). */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Enemy|Readability")
	static USoundBase* GetTelegraphSound(EBH_EnemyArchetype Archetype);

	/** Veteran, Elite and Boss show a name over the head. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Enemy|Readability")
	static bool RankShowsName(EBH_EnemyRank Rank) { return Rank != EBH_EnemyRank::Common; }

	/** Elite and Boss show a title. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Enemy|Readability")
	static bool RankShowsTitle(EBH_EnemyRank Rank) { return Rank == EBH_EnemyRank::Elite || Rank == EBH_EnemyRank::Boss; }

	static FText RankLabel(EBH_EnemyRank Rank);
	static FText ArchetypeLabel(EBH_EnemyArchetype Archetype);
};
