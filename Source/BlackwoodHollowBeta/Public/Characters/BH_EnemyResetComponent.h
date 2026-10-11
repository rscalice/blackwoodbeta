// Blackwood Hollow - enemy reset after a party wipe (Phase 11F)
// Target: Unreal Engine 5.8 (C++), GAS
//
// Server-only helper on every enemy (humanoids with a UBH_CombatIdentityComponent, crabs, bosses). It remembers the spawn
// transform and, when the party wipes (UBH_PartyStateSubsystem::OnPartyWiped -> ResetAllInWorld), puts the enemy back to a
// fresh state:
//   * Health and Posture back to 100 % of their current maximum, PostureBroken cleared, every ability cancelled,
//   * every status effect (UBH_GE_StatusEffect) removed,
//   * aggro cleared (identity / ABH_EnemyBase aggro target, AI controller target, held attack token),
//   * returned to the spawn transform: teleported when no player can see it, otherwise walked home (direct movement input, the
//     PrototypeBlockout has no navmesh) and teleported anyway after MaxWalkSeconds,
//   * boss / elite hook: the phase index goes back to 0, every registered add and temporary hazard is destroyed, and
//     OnWipeReset fires so a boss Blueprint can restore its own phase state.
// Dead enemies are left alone (the wave spawner respawns the dead enemies of the CURRENT wave itself).
// NOT touched on purpose: world progress (cleared fog, opened containers, picked-up shards, harvest timers).
//
// Attached automatically: ABH_EnemyBase::BeginPlay and UBH_CombatIdentityComponent::BeginPlay call EnsureOn on the server, so no
// Blueprint needs editing. Adding the component by hand in a Blueprint works too (EnsureOn then finds it).

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BH_EnemyResetComponent.generated.h"

class UBH_EnemyResetComponent;

/** SERVER. This enemy was reset by a party wipe (after stats / aggro / adds were reset, before the walk home finishes). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBH_OnEnemyWipeReset, UBH_EnemyResetComponent*, ResetComponent);

UCLASS(ClassGroup = (BlackwoodHollow), meta = (BlueprintSpawnableComponent))
class BLACKWOODHOLLOWBETA_API UBH_EnemyResetComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBH_EnemyResetComponent();

	/** The reset component on Actor, or null. */
	UFUNCTION(BlueprintPure, Category = "BH|Reset")
	static UBH_EnemyResetComponent* Find(const AActor* Actor);

	/** SERVER. The reset component on Actor, creating one when missing (the spawn transform is captured now). Null on a client. */
	static UBH_EnemyResetComponent* EnsureOn(AActor* Actor);

	/** SERVER. Resets every enemy of World. Returns how many were reset (dead ones are skipped). */
	static int32 ResetAllInWorld(const UWorld* ForWorld);

	// -- Configuration -------------------------------------------------------------------------

	/** false = this enemy ignores party wipes (a stationary prop with an ASC, say). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Reset")
	bool bResetOnPartyWipe = true;

	/** A player further than this cannot "see" the enemy: it is teleported home instead of walked. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Reset", meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float SightCheckDistance = 6000.f;

	/** Within this distance of the spawn point (2D) the walk home counts as arrived. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Reset", meta = (ClampMin = "10.0", ForceUnits = "cm"))
	float ArriveRadius = 150.f;

	/** The walk home gives up after this long and teleports (stuck on geometry, no way through). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Reset", meta = (ClampMin = "1.0", ForceUnits = "s"))
	float MaxWalkSeconds = 12.f;

	/** Seconds between "can a player see me" re-checks while walking home (unseen = teleport right away). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Reset", meta = (ClampMin = "0.05", ForceUnits = "s"))
	float SightRecheckInterval = 0.25f;

	/** Minimum dot product between the player's view direction and the direction to the enemy for it to count as "on screen". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Reset", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float SightMinDot = 0.3f;

	// -- Reset ---------------------------------------------------------------------------------

	/** SERVER. Runs the full reset (does nothing for a dead enemy or when bResetOnPartyWipe is off). */
	UFUNCTION(BlueprintCallable, Category = "BH|Reset")
	void ResetNow();

	UFUNCTION(BlueprintPure, Category = "BH|Reset")
	FTransform GetHomeTransform() const { return HomeTransform; }

	/** Moves the home point (a boss that was repositioned on purpose). */
	UFUNCTION(BlueprintCallable, Category = "BH|Reset")
	void SetHomeTransform(const FTransform& NewHome);

	/** True while the enemy is walking back to its spawn point. */
	UFUNCTION(BlueprintPure, Category = "BH|Reset")
	bool IsReturningHome() const { return bReturning; }

	// -- Boss / elite hook ---------------------------------------------------------------------

	/** Boss phase index (0 = the opening phase). A reset puts it back to 0; the boss Blueprint drives it with SetPhaseIndex. */
	UFUNCTION(BlueprintPure, Category = "BH|Reset|Boss")
	int32 GetPhaseIndex() const { return PhaseIndex; }

	UFUNCTION(BlueprintCallable, Category = "BH|Reset|Boss")
	void SetPhaseIndex(int32 NewPhaseIndex) { PhaseIndex = FMath::Max(0, NewPhaseIndex); }

	/** SERVER. Registers an add the boss summoned: a reset destroys it. */
	UFUNCTION(BlueprintCallable, Category = "BH|Reset|Boss")
	void RegisterAdd(AActor* Add);

	/** SERVER. Registers a temporary hazard (fire patch, blight pool) the boss created: a reset destroys it. */
	UFUNCTION(BlueprintCallable, Category = "BH|Reset|Boss")
	void RegisterHazard(AActor* Hazard);

	/** SERVER. Fires on every reset after stats, aggro, adds / hazards and the phase index were reset. */
	UPROPERTY(BlueprintAssignable, Category = "BH|Reset|Boss")
	FBH_OnEnemyWipeReset OnWipeReset;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void CaptureHome();
	bool IsOwnerDead() const;
	bool IsSeenByAnyPlayer() const;
	void TeleportHome();
	void FinishReturn();
	void SetControllerResetHold(bool bHold);
	void DestroyRegistered(TArray<TWeakObjectPtr<AActor>>& Registered);

	FTransform HomeTransform = FTransform::Identity;
	bool bHomeCaptured = false;
	bool bReturning = false;
	float ReturnElapsed = 0.f;
	float SightRecheckLeft = 0.f;
	int32 PhaseIndex = 0;

	TArray<TWeakObjectPtr<AActor>> RegisteredAdds;
	TArray<TWeakObjectPtr<AActor>> RegisteredHazards;
};
