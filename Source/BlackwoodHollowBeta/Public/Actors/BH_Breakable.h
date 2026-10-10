// Blackwood Hollow - a breakable world blocker (Phase 12F)
// Target: Unreal Engine 5.8 (C++)
//
// The Island 1 coral barricade (and any later "smash it to get through" prop). Melee swings reach it through IBH_Damageable: ANS_MeleeHitbox
// calls BH_ReceiveMeleeHit on the SERVER for the hit actor. Each hit removes DamagePerHit * the swing's damage multiplier from Health, so a
// heavy attack (higher multiplier) takes it down in fewer swings. At zero it breaks: bBroken replicates, every machine hides it, switches
// collision off and stops it blocking navigation.
//
// World state, not wipe state: nothing resets on a party wipe, a broken barricade stays broken. Only a level reload restores it.
//
// WORLD FLAG (#85): the actor's own bBroken / Health can be missed by a client that was out of range (or had its World Partition cell unloaded)
// when they changed. So BreakNow also sets a world flag on ABH_GameState (always relevant, replicates to every client), and EVERY machine applies
// the broken state from that flag in BeginPlay and on OnWorldFlagChanged (retrying until the game state exists). BrokenWorldFlag defaults to
// "Breakable.<actor FName>", the level actor's stable name (not its label). bAlwaysRelevant is set as belt and braces. Health still replicates
// for the in-range case.
//
// NAVIGATION: the break removes the blocker from the nav build, but the RecastNavMesh only rebuilds the hole at runtime when its Runtime
// Generation is Dynamic (it is Static in L_WreckageShallows at the time of writing; see the 12F post-build checklist).
//
// Setup in a Blueprint child: set BodyMesh to the coral graybox mesh and scale, tune MaxHealth / DamagePerHit on the instance.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/TimerHandle.h"
#include "Combat/BH_Damageable.h"
#include "BH_Breakable.generated.h"

class UStaticMeshComponent;

UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API ABH_Breakable : public AActor, public IBH_Damageable
{
	GENERATED_BODY()

public:
	ABH_Breakable();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Total hit points. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Breakable", meta = (ClampMin = "1"))
	float MaxHealth = 100.f;

	/** Hit points removed by a swing with a damage multiplier of 1.0 (a light attack). Heavier swings remove proportionally more. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Breakable", meta = (ClampMin = "0.1"))
	float DamagePerHit = 25.f;

	/** Swings with a multiplier below this do nothing. 0 = every swing counts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Breakable", meta = (ClampMin = "0"))
	float MinHitMultiplier = 0.f;

	/** Hide the mesh when broken. Turn off to keep a rubble mesh in place (collision and nav blocking still go away). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Breakable")
	bool bHideWhenBroken = true;

	/** The world flag that records this one as broken. Empty = derive "Breakable.<actor FName>" (stable: the level actor's name, not its label). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Breakable")
	FName BrokenWorldFlag;

	/** BrokenWorldFlag, or the derived "Breakable.<actor FName>". */
	UFUNCTION(BlueprintPure, Category = "BH|Breakable")
	FName GetResolvedBrokenFlag() const;

	UFUNCTION(BlueprintPure, Category = "BH|Breakable")
	bool IsBroken() const { return bAppliedBroken; }

	UFUNCTION(BlueprintPure, Category = "BH|Breakable")
	float GetHealth() const { return Health; }

	/** SERVER. Breaks it now (debug, or a script). */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "BH|Breakable")
	void BreakNow();

	/** Both machines, after Health changed (a hit landed). For a crack / shake effect. */
	UFUNCTION(BlueprintImplementableEvent, Category = "BH|Breakable")
	void BP_OnHealthChanged(float NewHealth);

	/** Both machines, after bBroken changed (also on a late joiner). For a break effect. */
	UFUNCTION(BlueprintImplementableEvent, Category = "BH|Breakable")
	void BP_OnBrokenChanged(bool bNewBroken);

	// -- IBH_Damageable -------------------------------------------------------------------------
	virtual void BH_ReceiveMeleeHit(AActor* Attacker, float DamageMultiplier, const FHitResult& Hit) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> BodyMesh;

private:
	UFUNCTION()
	void OnRep_Health();

	UFUNCTION()
	void OnRep_Broken();

	/** Delegate target for ABH_GameState::OnWorldFlagChanged (any machine). */
	UFUNCTION()
	void HandleWorldFlagChanged(FName Flag, bool bIsSet);

	/** Any machine: binds to the game state, or re-arms a retry while it is not there yet. */
	void TryBindGameState();

	/** True when this machine should show it broken: the replicated bBroken, or the world flag is set. */
	bool ComputeEffectiveBroken() const;

	/** Applies the visible / collision / nav consequences once, when the effective broken state first becomes true. */
	void RefreshBrokenState();

	UPROPERTY(ReplicatedUsing = OnRep_Health)
	float Health = 100.f;

	UPROPERTY(ReplicatedUsing = OnRep_Broken)
	bool bBroken = false;

	FTimerHandle BindRetryTimer;
	bool bBoundToGameState = false;

	/** What this machine currently shows (local, not replicated). */
	bool bAppliedBroken = false;
};
