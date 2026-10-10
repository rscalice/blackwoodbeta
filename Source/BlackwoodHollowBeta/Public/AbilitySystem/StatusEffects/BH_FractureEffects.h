// Blackwood Hollow - Heart-Fragment Fracture (Phase 11G)
// Target: Unreal Engine 5.8 (C++), GAS (GameplayAbilities plugin required)
//
// Fracture is the price of a hub respawn: the whole party is "fractured" when ANY member respawns at the hub (a partner revive does not).
//
//   UAH_GE_Fracture   Status effect (UBH_GE_StatusEffect, grants State.Status.Fracture). Infinite duration, one stack:
//                       - MaxPosture *= 0.90 (MultiplyAdditive, so it stacks additively with equipment mods and vanishes cleanly)
//                       - Heart-Fragment shielding efficiency *= 0.75 (read by UBPC_HeartFragment::GetShieldingFraction through
//                         UBH_FractureLibrary::GetShieldingEfficiencyMultiplier: 15% -> 11.25%)
//                     Re-applying while active does nothing. The HUD shows it through the normal status-effect path (Icon on the GE).
//   UBH_FractureLibrary  Apply / remove / query helpers. Apply and remove are server-only. A Blueprint child asset
//                     (/Game/BlackwoodHollow/Blueprints/Status/GE_Fracture) is used when it exists -- it carries the HUD icon --
//                     otherwise the C++ class is applied.
//
// Repair: ABH_RespawnPoint (Port Vanguard) removes it from ONE player for 3 Corrupted Coral Shards.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/StatusEffects/BH_StatusEffect.h"
#include "GameplayEffect.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "NativeGameplayTags.h"
#include "BH_FractureEffects.generated.h"

class AActor;
class UAbilitySystemComponent;

BLACKWOODHOLLOWBETA_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_State_Status_Fracture);

/** Fracture: -10% Max Posture, -25% Heart-Fragment shielding efficiency, no expiry, one stack, grants State.Status.Fracture. */
UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API UAH_GE_Fracture : public UBH_GE_StatusEffect
{
	GENERATED_BODY()

public:
	UAH_GE_Fracture();

	/** Heart-Fragment shielding efficiency while fractured (0.75 = -25%; 15% shielding -> 11.25%). */
	static constexpr float ShieldingEfficiencyMultiplier = 0.75f;

	/** Max Posture multiplier while fractured (0.9 = -10%). */
	static constexpr float MaxPostureMultiplier = 0.9f;

	/** Corrupted Coral Shards the repair at Port Vanguard costs. */
	static constexpr int32 RepairShardCost = 3;
};

UCLASS()
class BLACKWOODHOLLOWBETA_API UBH_FractureLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** True when the ASC carries State.Status.Fracture (works on the server and the owning client). */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Fracture")
	static bool IsFractured(const UAbilitySystemComponent* ASC);

	/** Same, for a pawn / controller / player state (resolves its ASC). */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Fracture")
	static bool IsActorFractured(const AActor* Actor);

	/** 0.75 while fractured, else 1. Multiplied into the Heart-Fragment shielding fraction. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Fracture")
	static float GetShieldingEfficiencyMultiplier(const UAbilitySystemComponent* ASC);

	/** Applies Fracture (authority only). Does nothing when already fractured (no stacking, no refresh). @return true if newly applied. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Fracture")
	static bool ApplyFracture(UAbilitySystemComponent* ASC);

	/** Removes Fracture (authority only). @return true if it was active. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Fracture")
	static bool RemoveFracture(UAbilitySystemComponent* ASC);

	/** Applies Fracture to every party member (authority only; includes the living partners and the caller). @return members newly fractured. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Fracture", meta = (WorldContext = "WorldContext"))
	static int32 ApplyFractureToParty(const UObject* WorldContext);

	/** The ASC of a pawn / controller / player state (pawn first, then its player state). Null for anything else. */
	static UAbilitySystemComponent* ResolveASC(const AActor* Actor);

	/** The GE class applied: the optional Blueprint child GE_Fracture, else UAH_GE_Fracture. */
	static TSubclassOf<UGameplayEffect> ResolveFractureEffectClass();
};
