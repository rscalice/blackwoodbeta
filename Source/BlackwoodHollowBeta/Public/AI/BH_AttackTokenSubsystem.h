// Blackwood Hollow - attack tokens: one (configurable) active attacker per target, the swarm mutex
// Target: Unreal Engine 5.8 (C++)
//
// A server-authoritative world subsystem. ABH_AIController asks RequestToken(Target, this) right before it would start a swing;
// without the token it circles (EBH_AIState::Circle) and keeps asking every tick.
//
// RULES
//   Holders .......... at most MaxTokensPerTarget per target. A holder's request is always true (idempotent).
//   Handoff .......... a token is only granted HandoffCooldown seconds after the target's last release (a breather between attackers).
//   Waiters .......... a denied requester is registered (first / last request time). A waiter whose last request is older than
//                      WaiterStaleTime (0.35 s) is dropped, so a requester that stops asking (circling ended, died) never blocks the queue.
//   Choosing ......... when a token is free, ONLY the best-scored fresh waiter gets it; RequestToken returns true for that waiter alone.
//                      Score = seconds waited (the main term)
//                            + FrontBonusSeconds if the requester's pawn is within FrontHalfAngleDeg of the target's view direction
//                              (control rotation for a player, actor forward for an AI), so attackers come from where the player looks
//                            - DistancePenaltyPerMeter x distance in metres.
//                      Ties break by first-request time, then controller unique id: the order is deterministic.
//   Auto release ..... holder / target invalid or unpossessed, holder pawn Dead / PostureBroken / Staggered, or held longer than MaxHoldTime.
//   Not replicated: AI controllers only exist on the server, so the subsystem is not created on pure clients.
//
// CONFIG (DefaultGame.ini, [/Script/BlackwoodHollowBeta.BH_AttackTokenSubsystem]): MaxTokensPerTarget, HandoffCooldown, MaxHoldTime,
// FrontBonusSeconds, DistancePenaltyPerMeter, bEnableAttackTokens (read once at Initialize into the static bEnableAttackTokens).
// Console: bh.AttackTokens.Debug 1 draws "TOKEN" above holders and "wait Ns" above waiters (server world: visible on a
// listen-server host / standalone), and also prints an on-screen summary per target, which shows in every PIE viewport
// (so a client window under PIE_Client / dedicated-server mode sees it too).

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "BH_AttackTokenSubsystem.generated.h"

class AActor;
class AController;

DECLARE_LOG_CATEGORY_EXTERN(LogBHTokens, Log, All);

UCLASS(config = Game)
class BLACKWOODHOLLOWBETA_API UBH_AttackTokenSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// -- Settings (config=Game) ---------------------------------------------------------

	/** Simultaneous attackers allowed per target. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Tokens", meta = (ClampMin = "1"))
	int32 MaxTokensPerTarget = 1;

	/** Seconds after a target's last token release before the next grant. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Tokens", meta = (ClampMin = "0"))
	float HandoffCooldown = 0.45f;

	/** A token held longer than this is taken back. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Tokens", meta = (ClampMin = "0.5"))
	float MaxHoldTime = 6.f;

	/** The in-front bonus, expressed as equivalent seconds of waiting. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Tokens", meta = (ClampMin = "0"))
	float FrontBonusSeconds = 1.f;

	/** Score penalty (in seconds of waiting) per metre between requester and target. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Tokens", meta = (ClampMin = "0"))
	float DistancePenaltyPerMeter = 0.1f;

	/** Half angle (degrees) of the in-front-of-the-target cone. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Tokens", meta = (ClampMin = "1", ClampMax = "180"))
	float FrontHalfAngleDeg = 70.f;

	/** A waiter that has not asked for this long is dropped. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Tokens", meta = (ClampMin = "0.05"))
	float WaiterStaleTime = 0.35f;

	/** Global switch (config bEnableAttackTokens, read at Initialize). false: every request is granted and nothing is tracked. */
	static bool bEnableAttackTokens;

	// -- API (server) -------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Tokens", meta = (WorldContext = "WorldContext", DisplayName = "Get Attack Token Subsystem"))
	static UBH_AttackTokenSubsystem* Get(const UObject* WorldContext);

	/** True when Requester holds (or has just been granted) a token on Target. Otherwise registers / refreshes it as a waiter and returns false. */
	bool RequestToken(AActor* Target, AController* Requester);

	/** Gives the token back (and drops any waiting entry). Starts the target's handoff cooldown if Holder held one. Safe with null / non-holders. */
	void ReleaseToken(AActor* Target, AController* Holder);

	UFUNCTION(BlueprintPure, Category = "Tokens")
	bool HasToken(const AActor* Target, const AController* Holder) const;

	/** The first holder's pawn on Target (nullptr if none). Debug helper. */
	UFUNCTION(BlueprintPure, Category = "Tokens")
	AActor* GetHolder(const AActor* Target) const;

	UFUNCTION(BlueprintPure, Category = "Tokens")
	int32 GetHolderCount(const AActor* Target) const;

	UFUNCTION(BlueprintPure, Category = "Tokens")
	int32 GetWaiterCount(const AActor* Target) const;

	// -- Test counters (this world) ------------------------------------------------------

	/** Tokens granted since the world started. */
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Tokens|Stats")
	int32 StatGrants = 0;

	/** Highest number of simultaneous holders ever seen on any single target (must stay <= MaxTokensPerTarget). */
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Tokens|Stats")
	int32 StatMaxHoldersObserved = 0;

	// -- USubsystem / tickable ------------------------------------------------------------
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

private:
	struct FHolder
	{
		TWeakObjectPtr<AController> Controller;
		double GrantTime = 0.0;
	};

	struct FWaiter
	{
		TWeakObjectPtr<AController> Controller;
		double FirstRequest = 0.0;
		double LastRequest = 0.0;
	};

	struct FTargetTokens
	{
		TArray<FHolder> Holders;
		TArray<FWaiter> Waiters;
		double LastReleaseTime = -1.0e9;
	};

	double Now() const;
	float ScoreWaiter(const FWaiter& Waiter, const AActor* Target, double Time) const;
	bool ShouldAutoRelease(const FHolder& Holder, const AActor* Target, double Time) const;
	void PruneWaiters(FTargetTokens& Entry, double Time) const;
	void DrawDebug(double Time) const;

	TMap<TWeakObjectPtr<AActor>, FTargetTokens> Targets;
	float TickAccumulator = 0.f;
};
