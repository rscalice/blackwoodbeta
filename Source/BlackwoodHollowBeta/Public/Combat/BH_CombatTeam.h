// Blackwood Hollow - Combat teams (friendly-fire filtering)
// Target: Unreal Engine 5.8 (C++)
//
// Teams map onto the engine's FGenericTeamId (AIModule) so the same IDs work
// for AI perception later. Resolution order for any actor lives in
// UBH_CombatFunctionLibrary::GetCombatTeamId():
//   1) the actor itself implements IGenericTeamAgentInterface (ABH_EnemyBase does)
//   2) a pawn's controller implements it and has a team set
//   3) a pawn with a human (non-bot) PlayerState -> Players
//   4) anything else -> no team (always hittable)
// Non-pawn actors (projectiles, traps) resolve through their Instigator pawn.

#pragma once

#include "CoreMinimal.h"
#include "GenericTeamAgentInterface.h"
#include "BH_CombatTeam.generated.h"

UENUM(BlueprintType)
enum class EBH_CombatTeam : uint8
{
	/** No team: hostile to / hittable by everyone. Maps to FGenericTeamId::NoTeam. */
	Neutral = 0,

	/** Player-controlled Vanguards. Players never damage each other. */
	Players = 1,

	/** Hollow enemies (ABH_EnemyBase default). Enemies never damage each other. */
	Enemies = 2,
};

namespace BH_CombatTeam
{
	inline FGenericTeamId ToGenericTeamId(EBH_CombatTeam Team)
	{
		return Team == EBH_CombatTeam::Neutral ? FGenericTeamId::NoTeam : FGenericTeamId(static_cast<uint8>(Team));
	}

	inline EBH_CombatTeam FromGenericTeamId(FGenericTeamId TeamId)
	{
		switch (TeamId.GetId())
		{
		case static_cast<uint8>(EBH_CombatTeam::Players): return EBH_CombatTeam::Players;
		case static_cast<uint8>(EBH_CombatTeam::Enemies): return EBH_CombatTeam::Enemies;
		default: return EBH_CombatTeam::Neutral;
		}
	}
}
