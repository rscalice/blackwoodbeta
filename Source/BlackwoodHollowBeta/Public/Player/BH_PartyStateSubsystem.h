// Blackwood Hollow - party combat / wipe state (Phase 10B part 2: player death flow)
// Target: Unreal Engine 5.8 (C++)
//
// Server-authoritative world subsystem (not created on pure clients). It answers two questions for the death flow:
//
//   IS THE PARTY IN COMBAT?  Yes while any LIVING enemy's replicated AggroTarget (UBH_CombatIdentityComponent) is a LIVING player.
//                            It stays "in combat" until OutOfCombatGrace seconds have passed with none. The aggro sources are
//                            tracked event-driven through UBH_CombatIdentityComponent::OnAnyAggroTargetChanged and re-evaluated
//                            every EvaluateInterval seconds (the grace timer, an enemy or its target dying, ...).
//                            When the party leaves combat every dead player's revive window can open
//                            (UBH_PlayerDeathComponent::TryOpenReviveWindow); when it re-enters combat OnPartyCombatChanged(true)
//                            fires (an in-progress revive is interrupted by the death component).
//
//   IS THE PARTY WIPED?      Every PARTY MEMBER's pawn is dead (Phase 11P: members come from UBH_PartyLibrary / ABH_GameState). OnPartyWiped fires once per wipe (it re-arms when somebody is alive again).
//                            TODO(GDD 5.5): party wipe consequences - enemies reset, encounters do NOT. Nothing listens to this yet.
//
// Config (DefaultGame.ini, [/Script/BlackwoodHollowBeta.BH_PartyStateSubsystem]): OutOfCombatGrace, EvaluateInterval.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Engine/TimerHandle.h"
#include "BH_PartyStateSubsystem.generated.h"

class AActor;
class UBH_CombatIdentityComponent;
class UBH_PlayerDeathComponent;

/** Server: every player is dead. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FBH_OnPartyWiped);

/** Server: the party entered (true) or left (false) combat. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBH_OnPartyCombatChanged, bool, bInCombat);

UCLASS(config = Game)
class BLACKWOODHOLLOWBETA_API UBH_PartyStateSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Seconds with no living enemy aggroed on a living player before the party counts as out of combat. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "BH|Party", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float OutOfCombatGrace = 3.f;

	/** Seconds between combat re-evaluations. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "BH|Party", meta = (ClampMin = "0.05", ForceUnits = "s"))
	float EvaluateInterval = 0.25f;

	/** Server: the party was wiped (all players dead). TODO(GDD 5.5): enemies reset, encounters do not. */
	UPROPERTY(BlueprintAssignable, Category = "BH|Party")
	FBH_OnPartyWiped OnPartyWiped;

	/** Server: the party entered / left combat. */
	UPROPERTY(BlueprintAssignable, Category = "BH|Party")
	FBH_OnPartyCombatChanged OnPartyCombatChanged;

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	UFUNCTION(BlueprintPure, Category = "BH|Party", meta = (WorldContext = "WorldContext", DisplayName = "Get Party State Subsystem"))
	static UBH_PartyStateSubsystem* Get(const UObject* WorldContext);

	/** Server: true while the party is in combat (including the OutOfCombatGrace tail). */
	UFUNCTION(BlueprintPure, Category = "BH|Party")
	bool IsPartyInCombat() const { return bInCombat; }

	/** Server: true if a player pawn other than the one owning Excluding is alive. */
	bool HasOtherLivingPlayer(const UBH_PlayerDeathComponent* Excluding) const;

	/** Server: a player died or came back to life: re-checks the wipe immediately (no waiting for the timer). */
	void NotifyPlayerStateChanged();

private:
	void Evaluate();
	void EvaluateWipe();
	void OpenReviveWindows();
	/** Server: mirrors the combat flag onto ABH_GameState. */
	void PublishCombatState(bool bNowInCombat) const;
	void HandleAggroTargetChanged(UBH_CombatIdentityComponent* Source, AActor* NewTarget);

	/** Runs Fn for the death component of every player pawn (server). */
	void ForEachPlayerComponent(TFunctionRef<void(UBH_PlayerDeathComponent&)> Fn) const;

	/** Enemies that currently have an aggro target (weak; pruned in Evaluate). */
	TSet<TWeakObjectPtr<UBH_CombatIdentityComponent>> AggroSources;

	FDelegateHandle AggroHandle;
	FTimerHandle EvaluateTimer;
	double LastEngagedTime = -1.0e9;
	bool bInCombat = false;
	bool bWipeLatched = false;
};
