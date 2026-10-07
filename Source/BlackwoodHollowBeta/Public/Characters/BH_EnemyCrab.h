// Blackwood Hollow - crab enemy (Phase 8C)
// Target: Unreal Engine 5.8 (C++), GAS
//
// A small melee enemy that fights with three procedural abilities (UAH_GA_CrabJab / Pinch / Sidestep) under a Behavior Tree
// (ABH_CrabAIController). Make a Blueprint child (BP_BH_Enemy_Crab) to assign the mesh (scaled 1.7), anim class (ABP_BH_Crab),
// physics asset, DefaultAbilities (HitReaction, PostureBreak, Jab, Pinch, Sidestep), CrabBehaviorTree and the telegraph assets.
//
// ACTION PHASE: the anim instance cannot see server-only GAS state, and loose tags do not replicate, so the crab keeps a
// replicated EBH_CrabActionPhase. Priority: Dead > Stagger (PostureBroken tag) > HitReact (Staggered tag) > the phase the
// running ability reported through SetAbilityPhase.
//
// DEATH: extends the ABH_EnemyBase death path (OnDeathNative): the capsule stops colliding, the mesh switches to the Ragdoll
// collision profile and simulates its physics asset (on every machine, driven by the replicated Dead phase), and the actor
// despawns DespawnDelay seconds later.
//
// STATS: ApplyInitialStats (BlueprintNativeEvent) runs on the server at BeginPlay. The defaults (MaxHealth 45, MaxPosture 30)
// are EditDefaultsOnly, so the Blueprint can change them; untick bApplyStatOverrides to keep the attribute set's own defaults.

#pragma once

#include "CoreMinimal.h"
#include "Characters/BH_EnemyBase.h"
#include "Characters/BH_CrabTypes.h"
#include "GameplayTagContainer.h"
#include "BH_EnemyCrab.generated.h"

class UBH_TelegraphComponent;
class UBehaviorTree;

UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API ABH_EnemyCrab : public ABH_EnemyBase
{
	GENERATED_BODY()

public:
	ABH_EnemyCrab();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// -- Components ---------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Crab")
	UBH_TelegraphComponent* GetTelegraph() const { return Telegraph; }

	// -- Action phase -------------------------------------------------------------------------

	/** The phase the anim instance should show (replicated). */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Crab")
	EBH_CrabActionPhase GetActionPhase() const { return ActionPhase; }

	/** SERVER: the running crab ability reports what it is doing (Idle when it ends). Hit-react / stagger / dead still win. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Crab")
	void SetAbilityPhase(EBH_CrabActionPhase NewPhase);

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Crab")
	bool IsCrabDead() const { return ActionPhase == EBH_CrabActionPhase::Dead; }

	/** True while State.Status.Blighted is on this crab (the tag replicates through the GE's minimal replication). */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Crab")
	bool IsBlighted() const;

	// -- Sidestep bookkeeping (server) --------------------------------------------------------

	/** Seconds since the last sidestep started (a huge number if it never sidestepped). */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Crab")
	float GetSecondsSinceLastSidestep() const;

	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Crab")
	void MarkSidestepUsed();

	// -- Tunables -----------------------------------------------------------------------------

	/** Behavior tree run by ABH_CrabAIController (it falls back to its own DefaultBehaviorTree when empty). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BlackwoodHollow|Crab|AI")
	TObjectPtr<UBehaviorTree> CrabBehaviorTree;

	/** Cosmetic variant flag for Blueprints (alternate material / VFX). No gameplay effect. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BlackwoodHollow|Crab")
	bool bBlightedVariant = false;

	/** Seconds the ragdoll stays before the actor is destroyed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BlackwoodHollow|Crab|Death", meta = (ClampMin = "0.1"))
	float DespawnDelay = 4.f;

	/** Mesh collision profile used while ragdolling. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BlackwoodHollow|Crab|Death")
	FName RagdollCollisionProfile = TEXT("Ragdoll");

	/** Apply the Initial* stats below at BeginPlay (server). Untick to keep the attribute set's defaults. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BlackwoodHollow|Crab|Stats")
	bool bApplyStatOverrides = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BlackwoodHollow|Crab|Stats", meta = (EditCondition = "bApplyStatOverrides", ClampMin = "1"))
	float InitialMaxHealth = 45.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BlackwoodHollow|Crab|Stats", meta = (EditCondition = "bApplyStatOverrides", ClampMin = "1"))
	float InitialMaxPosture = 30.f;

	/** < 0 = leave the attribute set's value. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BlackwoodHollow|Crab|Stats", meta = (EditCondition = "bApplyStatOverrides"))
	float InitialAttackPower = -1.f;

	/** < 0 = leave the attribute set's value. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BlackwoodHollow|Crab|Stats", meta = (EditCondition = "bApplyStatOverrides"))
	float InitialDefense = -1.f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnDeathNative(AActor* Killer) override;

	/** Server, BeginPlay: writes the Initial* stats into the attribute set. Override per Blueprint to do something else. */
	UFUNCTION(BlueprintNativeEvent, Category = "BlackwoodHollow|Crab|Stats")
	void ApplyInitialStats();
	virtual void ApplyInitialStats_Implementation();

	/** Switches the body to physics (capsule off, mesh simulating). Idempotent; runs on every machine. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Crab|Death")
	void StartRagdoll();

	/** Cosmetic hook fired on every machine when the ragdoll starts. */
	UFUNCTION(BlueprintImplementableEvent, Category = "BlackwoodHollow|Crab|Death", meta = (DisplayName = "On Ragdoll Started"))
	void K2_OnRagdollStarted();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BlackwoodHollow|Crab")
	TObjectPtr<UBH_TelegraphComponent> Telegraph;

	UPROPERTY(ReplicatedUsing = OnRep_ActionPhase, VisibleInstanceOnly, BlueprintReadOnly, Category = "BlackwoodHollow|Crab")
	EBH_CrabActionPhase ActionPhase = EBH_CrabActionPhase::Idle;

	UFUNCTION()
	void OnRep_ActionPhase();

private:
	void RecomputeActionPhase();
	void OnStatusTagChanged(const FGameplayTag Tag, int32 NewCount);

	EBH_CrabActionPhase AbilityPhase = EBH_CrabActionPhase::Idle;
	bool bDeadFlag = false;
	bool bRagdolling = false;
	double LastSidestepTime = -1.0e9;

	FDelegateHandle PostureBrokenHandle;
	FDelegateHandle StaggeredHandle;
};
