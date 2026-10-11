// Blackwood Hollow - per-island rules actor (Phase 12F)
// Target: Unreal Engine 5.8 (C++)
//
// A level-placed marker that switches the Island 1 start and progression gates on for the level it sits in. A level without one
// (PrototypeBlockout) behaves exactly as before. The three flags are plain level data, so they are the same on every machine; the
// SERVER applies them to each player's PlayerState once (polled every ApplyInterval, so late joiners are covered):
//
//   bStartUnarmed               the starter loadout is NOT granted (UBH_LoadoutComponent::TryGrantStarterLoadout asks FindRules), and
//                               attack / block / parry input does nothing while the player has no weapon equipped
//                               (UBH_CombatFunctionLibrary::HandleMeleeAttackInput / HandleBlockInput / HandleParryInput ask
//                               IsWeaponInputLocked on every machine). Dodge, sprint, interaction stay available.
//   bLockWeaponSetB             ABH_PlayerState::bWeaponSetBUnlocked starts false (the loadout and the radial refuse set B).
//   bStartWithoutFragmentSkills every Heart-Fragment slot is emptied (the HUD shows them dark); the Warden obelisk grants Overload Burst.
//
// Per-player state lives on the PlayerState, so a death, respawn or party wipe never re-applies or resets anything: each PlayerState is
// handled once per level.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/TimerHandle.h"
#include "BH_IslandRules.generated.h"

class ABH_PlayerState;
class APlayerState;
class USceneComponent;

UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API ABH_IslandRules : public AActor
{
	GENERATED_BODY()

public:
	ABH_IslandRules();

	/** Players start with no weapon sets; attack / block / parry do nothing until they equip one (the weapon cache). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BH|IslandRules")
	bool bStartUnarmed = true;

	/** Weapon set B starts locked for every player (ABH_PlayerState::IsWeaponSetBUnlocked false). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BH|IslandRules")
	bool bLockWeaponSetB = true;

	/** The Heart-Fragment slots start empty (no Overload Burst until the Warden obelisk). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BH|IslandRules")
	bool bStartWithoutFragmentSkills = true;

	/** Seconds between server passes that apply the rules to player states that have not been handled yet. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BH|IslandRules", meta = (ClampMin = "0.05", ForceUnits = "s"))
	float ApplyInterval = 0.2f;

	/** The rules actor of WorldContext's world, or null (no rules = the prototype behaviour). */
	static ABH_IslandRules* FindRules(const UObject* WorldContext);

	/**
	 * Any machine. True when the island starts players unarmed AND PlayerPawn has a loadout component with nothing equipped in either set.
	 * Enemies (no loadout component) and levels without rules always return false.
	 */
	static bool IsWeaponInputLocked(const AActor* PlayerPawn);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BH|IslandRules")
	TObjectPtr<USceneComponent> SceneRoot;

private:
	/** Server: applies the rules to every player state not handled yet. */
	void ApplyToNewPlayers();

	/** Server: applies the rules to one player state. */
	void ApplyToPlayerState(ABH_PlayerState* TargetState);

	TSet<TWeakObjectPtr<APlayerState>> HandledStates;
	FTimerHandle ApplyTimer;
};
