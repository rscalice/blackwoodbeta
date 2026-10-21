// Blackwood Hollow - partner panel data types (Phase 13I-1, "Resonance Link")
// Target: Unreal Engine 5.8 (C++)
//
// FBH_PartyMemberStatus is the small, always-relevant summary of one player that every client can read from that player's ABH_PlayerState
// (the pawn is not relevant beyond net cull distance, and BlightBuildup is owner-only replicated). The server owns it; see
// ABH_PlayerState::GetPartyStatus / OnPartyStatusChanged.

#pragma once

#include "CoreMinimal.h"
#include "Player/BH_PlayerDeathComponent.h"
#include "BH_PartyStatus.generated.h"

/** Connection quality of a player, derived from the PlayerState ping. */
UENUM(BlueprintType)
enum class EBH_PingQuality : uint8
{
	Good,
	Fair,
	Poor,
};

/** Replicated summary of one party member for the partner panel. Rounded so that tiny changes do not cause replication traffic. */
USTRUCT(BlueprintType)
struct BLACKWOODHOLLOWBETA_API FBH_PartyMemberStatus
{
	GENERATED_BODY()

	/** Health / MaxHealth, 0..1, rounded to 1%. */
	UPROPERTY(BlueprintReadOnly, Category = "Party")
	float HealthFraction = 1.f;

	/** Blight stacks 0..10 (BlightBuildup 0..100 divided by 10, rounded down). */
	UPROPERTY(BlueprintReadOnly, Category = "Party")
	int32 BlightStacks = 0;

	/** Alive / Dead (downed) / AwaitingChoice / WaitingForRevive, straight from UBH_PlayerDeathComponent. */
	UPROPERTY(BlueprintReadOnly, Category = "Party")
	EBH_DeathPhase DeathState = EBH_DeathPhase::Alive;

	/** The player's ASC carries State.Combat.InCombat. */
	UPROPERTY(BlueprintReadOnly, Category = "Party")
	bool bInCombat = false;

	/** The player's ASC carries State.Status.Fracture. */
	UPROPERTY(BlueprintReadOnly, Category = "Party")
	bool bFractured = false;

	/** Progression level. */
	UPROPERTY(BlueprintReadOnly, Category = "Party")
	int32 Level = 1;

	bool operator==(const FBH_PartyMemberStatus& Other) const
	{
		return HealthFraction == Other.HealthFraction
			&& BlightStacks == Other.BlightStacks
			&& DeathState == Other.DeathState
			&& bInCombat == Other.bInCombat
			&& bFractured == Other.bFractured
			&& Level == Other.Level;
	}

	bool operator!=(const FBH_PartyMemberStatus& Other) const { return !(*this == Other); }
};

/** A player state's FBH_PartyMemberStatus changed (server when it writes it, clients from the RepNotify). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBH_OnPartyStatusChanged, const FBH_PartyMemberStatus&, NewStatus);
