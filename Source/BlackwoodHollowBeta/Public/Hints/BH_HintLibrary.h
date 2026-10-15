// Blackwood Hollow - hint helpers (Phase 13E)
// Target: Unreal Engine 5.8 (C++)
//
// Server-side entry points for "this player just did X" (call them from gameplay code or Blueprints), plus the catalogue of hint assets.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BH_HintLibrary.generated.h"

class UBH_HintData;
class UBH_PlayerHintComponent;

UCLASS()
class BLACKWOODHOLLOWBETA_API UBH_HintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * SERVER. Fires every Manual hint whose TriggerKey is Key for the player behind Context (a pawn, a PlayerState or a PlayerController).
	 * Each hint is sent to its owner once per session; the client shows it once per profile. No-op on a client or for a non-player.
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Hints", meta = (DefaultToSelf = "Context"))
	static void TriggerHintKey(const UObject* Context, FName Key);

	/** SERVER. Fires one hint by id for the player behind Context, whatever its trigger type. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Hints", meta = (DefaultToSelf = "Context"))
	static void TriggerHint(const UObject* Context, FName HintId);

	/** Every hint asset in the hints folder (loaded once, cached). Empty until the asset registry has scanned the folder. */
	static const TArray<UBH_HintData*>& GetAllHints();

	/** The hint with this id, or null. */
	static UBH_HintData* FindHint(FName HintId);

	/** The PlayerState's hint component for the player behind Context, or null. */
	static UBH_PlayerHintComponent* FindPlayerHintComponent(const UObject* Context);
};
