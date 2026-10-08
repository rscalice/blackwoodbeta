// Blackwood Hollow - consumable definitions (Phase 11D)
// Target: Unreal Engine 5.8 (C++)
//
// A consumable is a Narrative item Blueprint (BI_Consumable_*) plus a FBH_ConsumableDefinition that tells UBH_GA_UseConsumable what using it
// does: which montage to play, how long the use takes, and the effect that lands when the use finishes.
//
// WHY A DATA ASSET (and not fields on a UBH_ConsumableItem subclass of UNarrativeItem):
//   * the two item Blueprints already exist with a Narrative parent class; re-parenting them needs this code to be compiled first,
//   * the effect needs references to montages / actors that the item layer (a plain UObject, not a gameplay asset) should not hard-load,
//   * a designer can add a third consumable by creating one DA_Consumable_* asset: no item re-parenting, no new ability, no code.
// UBH_ConsumableLibrary::FindDefinition scans the asset registry for UBH_ConsumableData assets by item class and falls back to the
// built-in defaults below (the locked balance.md section 9 numbers) for Heartwood Sap and Warden's Incense, so the system works even
// before any DA_Consumable_* asset exists.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "BH_ConsumableTypes.generated.h"

class ABH_WardenSanctuary;
class UAnimMontage;
class UNarrativeItem;

/** Balance values (claude/balance.md section 9). Plain constexpr literals: no static-init-order globals. */
namespace BH_ConsumableDefaults
{
	// Heartwood Sap
	inline constexpr float SapUseSeconds = 1.2f;
	inline constexpr float SapHealFraction = 0.45f;   // of MaxHealth, total
	inline constexpr float SapHealSeconds = 3.0f;
	// Warden's Incense
	inline constexpr float IncenseUseSeconds = 0.8f;
	inline constexpr float IncenseRadius = 600.f;     // 6 m
	inline constexpr float IncenseSeconds = 8.0f;

	inline constexpr const TCHAR* DrinkMontagePath = TEXT("/Game/BlackwoodHollow/Animation/Consumables/AM_BH_Consume_Drink.AM_BH_Consume_Drink");
	inline constexpr const TCHAR* CenserMontagePath = TEXT("/Game/BlackwoodHollow/Animation/Consumables/AM_BH_Consume_PlaceCenser.AM_BH_Consume_PlaceCenser");
}

UENUM(BlueprintType)
enum class EBH_ConsumableEffectKind : uint8
{
	/** Periodic heal on the user (UBH_GE_HeartwoodSapHeal). */
	HealOverTime,

	/** Drops a Warden sanctuary (ABH_WardenSanctuary) in front of the user. */
	DeploySanctuary
};

/** Everything UBH_GA_UseConsumable needs to know about one consumable. */
USTRUCT(BlueprintType)
struct BLACKWOODHOLLOWBETA_API FBH_ConsumableDefinition
{
	GENERATED_BODY()

	/** The Narrative item Blueprint this definition belongs to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable")
	TSoftClassPtr<UNarrativeItem> ItemClass;

	/** Played on the user (UpperBody slot). Optional: with none, the use still takes UseDuration. Its play rate is set so it lasts UseDuration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable")
	TSoftObjectPtr<UAnimMontage> Montage;

	/** Seconds from the press until the effect lands (and the ability ends). The user cannot attack, dodge, block or parry meanwhile. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable", meta = (ClampMin = "0.1", ForceUnits = "s"))
	float UseDuration = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable")
	EBH_ConsumableEffectKind EffectKind = EBH_ConsumableEffectKind::HealOverTime;

	// -- HealOverTime -----------------------------------------------------------------------------

	/** Total heal as a fraction of MaxHealth (0.45 = 45%). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Heal", meta = (EditCondition = "EffectKind == EBH_ConsumableEffectKind::HealOverTime", ClampMin = "0.0", ClampMax = "1.0"))
	float HealFractionOfMaxHealth = 0.45f;

	/** Seconds the heal is spread over. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Heal", meta = (EditCondition = "EffectKind == EBH_ConsumableEffectKind::HealOverTime", ClampMin = "0.25", ForceUnits = "s"))
	float HealDuration = 3.f;

	// -- DeploySanctuary ----------------------------------------------------------------------------

	/** Sanctuary class to spawn. Null = ABH_WardenSanctuary itself. A Blueprint child can carry the final visuals and audio. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Sanctuary", meta = (EditCondition = "EffectKind == EBH_ConsumableEffectKind::DeploySanctuary"))
	TSubclassOf<ABH_WardenSanctuary> SanctuaryClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Sanctuary", meta = (EditCondition = "EffectKind == EBH_ConsumableEffectKind::DeploySanctuary", ClampMin = "50.0", ForceUnits = "cm"))
	float SanctuaryRadius = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Sanctuary", meta = (EditCondition = "EffectKind == EBH_ConsumableEffectKind::DeploySanctuary", ClampMin = "0.5", ForceUnits = "s"))
	float SanctuaryDuration = 8.f;

	/** How far in front of the user the censer lands. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Consumable|Sanctuary", meta = (EditCondition = "EffectKind == EBH_ConsumableEffectKind::DeploySanctuary", ForceUnits = "cm"))
	float SpawnForwardOffset = 80.f;
};

/** One DA_Consumable_* asset: the definition of one consumable item. */
UCLASS(BlueprintType)
class BLACKWOODHOLLOWBETA_API UBH_ConsumableData : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Consumable")
	FBH_ConsumableDefinition Definition;
};
