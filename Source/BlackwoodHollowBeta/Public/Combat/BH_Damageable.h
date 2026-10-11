// Blackwood Hollow - minimal "takes melee hits" interface (Phase 12F)
// Target: Unreal Engine 5.8 (C++)
//
// For world props that react to the player's weapon but have no AbilitySystemComponent (the coral barricade ABH_Breakable).
// UANS_MeleeHitbox::ProcessHit calls BH_ReceiveMeleeHit on the hit actor, on the AUTHORITY only (the server decides; clients and
// simulated proxies never call it). Friendly-fire, dodge i-frame and once-per-swing filtering have already happened by then.

#pragma once

#include "CoreMinimal.h"
#include "Engine/HitResult.h"
#include "UObject/Interface.h"
#include "BH_Damageable.generated.h"

UINTERFACE(MinimalAPI)
class UBH_Damageable : public UInterface
{
	GENERATED_BODY()
};

/** Implemented by an actor that the player's melee hitbox can damage without GAS. */
class BLACKWOODHOLLOWBETA_API IBH_Damageable
{
	GENERATED_BODY()

public:
	/**
	 * SERVER only. A melee swing from Attacker connected with this actor.
	 * @param DamageMultiplier the hitbox window's DamageMultiplier (1 = a normal swing, higher for heavy attacks).
	 * @param Hit the sweep result (impact point / component), for VFX.
	 */
	virtual void BH_ReceiveMeleeHit(AActor* Attacker, float DamageMultiplier, const FHitResult& Hit) {}
};
