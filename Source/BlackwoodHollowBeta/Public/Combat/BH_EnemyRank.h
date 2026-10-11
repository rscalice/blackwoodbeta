// Blackwood Hollow - Phase 12G enemy rank / archetype data and the shared helpers every enemy class uses
// Target: Unreal Engine 5.8 (C++), GAS
//
// RANK is how tough an enemy is relative to its kind (Common < Veteran < Elite < Boss): per-rank Health / Posture / Damage / XP multipliers
// live in UBH_RPGSettings (Project Settings > Game > "Blackwood Hollow RPG" > Enemy Ranks) and are applied ON TOP of the CT_EnemyScaling level
// curve, on the server, at spawn. ARCHETYPE is the role (Skirmisher, Soldier, Brute, Defender, Caster, Swarm); it is data for now (debug label,
// future AI / UI) and changes no numbers.
//
// Both live on every enemy class: ABH_EnemyBase (crab, dummy) and UBH_CombatIdentityComponent (the motion-matching humanoids). This library
// is the one place that reads them, so HUD, XP, damage and debug code never care which class the enemy is.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BH_EnemyRank.generated.h"

class UAbilitySystemComponent;

UENUM(BlueprintType)
enum class EBH_EnemyRank : uint8
{
	Common,
	Veteran,
	Elite,
	Boss,
};

UENUM(BlueprintType)
enum class EBH_EnemyArchetype : uint8
{
	Skirmisher,
	Soldier,
	Brute,
	Defender,
	Caster,
	Swarm,
};

/** Multipliers for one rank. Health / Posture scale the MaxHealth / MaxPosture the level curve gave; Damage scales outgoing melee damage and posture damage; XP scales the kill XP. */
USTRUCT(BlueprintType)
struct FBH_EnemyRankMultipliers
{
	GENERATED_BODY()

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Rank", meta = (ClampMin = "0.01"))
	float Health = 1.f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Rank", meta = (ClampMin = "0.01"))
	float Posture = 1.f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Rank", meta = (ClampMin = "0.01"))
	float Damage = 1.f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Rank", meta = (ClampMin = "0.0"))
	float XP = 1.f;

	FBH_EnemyRankMultipliers() = default;
	FBH_EnemyRankMultipliers(float InHealth, float InPosture, float InDamage, float InXP)
		: Health(InHealth), Posture(InPosture), Damage(InDamage), XP(InXP)
	{
	}
};

UCLASS()
class BLACKWOODHOLLOWBETA_API UBH_EnemyRankLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** The rank multipliers from the RPG settings. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Enemy|Rank")
	static FBH_EnemyRankMultipliers GetMultipliers(EBH_EnemyRank Rank);

	/** Rank of any enemy (ABH_EnemyBase or identity-component character). Common for anything else (the player). */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Enemy|Rank")
	static EBH_EnemyRank GetRank(const AActor* Enemy);

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Enemy|Rank")
	static EBH_EnemyArchetype GetArchetype(const AActor* Enemy);

	/** True when the actor is an enemy of either class (has rank data). */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Enemy|Rank")
	static bool HasRankData(const AActor* Enemy);

	/** Rank damage multiplier of Enemy (1 for non-enemies). Folded into UBH_CombatIdentityComponent::GetOutgoingCombatMultiplier. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Enemy|Rank")
	static float GetDamageMultiplier(const AActor* Enemy);

	/** True when this enemy gets the big boss health bar: identity bIsBoss, or a rank that presents as a boss (Boss always; Elite when the setting says so). */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Enemy|Rank")
	static bool IsBossPresented(const AActor* Enemy);

	/** Level of either enemy class (1 for non-enemies). */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Enemy|Rank")
	static int32 GetEnemyLevel(const AActor* Enemy);

	/** Kill XP the enemy grants right now (table at its level times the rank XP multiplier); 0 for non-enemies. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Enemy|Rank")
	static int32 GetEnemyXPReward(const AActor* Enemy);

	/** Aggro target (replicated) of either enemy class. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Enemy|Rank")
	static AActor* GetAggroTarget(const AActor* Enemy);

	/** Name for the boss bar: the identity's BossDisplayName / DisplayName, or the ABH_EnemyBase DisplayName. Empty text when not an enemy. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Enemy|Rank")
	static FText GetBossBarName(const AActor* Enemy);

	/** SERVER. Multiplies MaxHealth / MaxPosture by the rank multipliers and refills Health / Posture. Call once, right after the level scaling wrote the base values. */
	static void ApplyRankToStats(UAbilitySystemComponent* ASC, EBH_EnemyRank Rank);

	/**
	 * SERVER. Writes Level and, for every row "<Prefix>.<Stat>" present in the enemy scaling table, the base MaxHealth (refilling Health), MaxPosture
	 * (refilling Posture), AttackPower and Defense. Missing rows leave the current value alone. Shared by ABH_EnemyBase and the identity component.
	 */
	static void ApplyLevelScalingToASC(UAbilitySystemComponent* ASC, FName Prefix, int32 Level);

	/** Table XP at Level (row "<Prefix>.XPReward"), else Fallback; then times the rank XP multiplier, rounded. */
	static int32 ComputeXPReward(FName Prefix, int32 Level, int32 Fallback, EBH_EnemyRank Rank);

	/**
	 * Call on a deferred-spawned enemy BEFORE FinishSpawning: overrides its rank (RankOverride < 0 = keep the Blueprint's) and level
	 * (LevelOverride <= 0 = keep). ABH_EnemyBase gets the properties set directly; an identity-component character cannot (its component does not
	 * exist until FinishSpawning), so the request is parked on the identity class and consumed by that actor's own BeginPlay.
	 */
	static void PrepareSpawn(AActor* Spawned, int32 RankOverride, int32 LevelOverride);

	static const TCHAR* RankName(EBH_EnemyRank Rank);
	static const TCHAR* ArchetypeName(EBH_EnemyArchetype Archetype);

	/** Parses "common" / "vet" / "veteran" / "elite" / "boss" (case-insensitive). */
	static bool ParseRank(const FString& Text, EBH_EnemyRank& OutRank);
};
