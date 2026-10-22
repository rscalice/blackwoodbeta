// Blackwood Hollow - party status data types (Phase 14A-3 prep)
// Target: Unreal Engine 5.8 (C++)
//
// FBH_PartnerPing used to live in Beats/BH_BeatTypes.h. It moved here so it survives the retirement of the beat tracker.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "BH_PartyStatusTypes.generated.h"

/** A party member as every machine sees them: replicated at about 4 Hz by the server, so it works for far players that are not net-relevant. */
USTRUCT(BlueprintType)
struct BLACKWOODHOLLOWBETA_API FBH_PartnerPing
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Beat")
	TObjectPtr<APlayerState> Player = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Beat")
	FVector_NetQuantize10 Location = FVector_NetQuantize10(0.0, 0.0, 0.0);

	/** EBH_DeathPhase as a byte: 0 = alive; anything else is down (1 dead, 2 awaiting choice, 3 waiting for revive). */
	UPROPERTY(BlueprintReadOnly, Category = "Beat")
	uint8 DeathPhase = 0;
};
