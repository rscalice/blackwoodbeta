// Blackwood Hollow - server-side hint trigger watcher (Phase 13E)
// Target: Unreal Engine 5.8 (C++)
//
// Lives on ABH_PlayerState (a default subobject, replicated). On the SERVER it watches its player for the trigger types in EBH_HintTrigger
// (state tags, attributes, an enemy engaging, a partner going down, the pawn becoming ready) and when a hint fires it sends ClientShowHint to
// the OWNING client only, so in co-op every player gets their own hints. Each hint is sent at most once per session per player; the client
// keeps the real once-per-profile record (UBH_HintSubsystem). A listen-server host is its own client, so the same path serves host and client.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Combat/BH_EnemyRank.h"
#include "BH_PlayerHintComponent.generated.h"

class APawn;
class UBH_HintData;
class UBH_CombatIdentityComponent;
class ABH_EnemyBase;

UCLASS(ClassGroup = (BlackwoodHollow), meta = (BlueprintSpawnableComponent))
class BLACKWOODHOLLOWBETA_API UBH_PlayerHintComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBH_PlayerHintComponent();

	/** SERVER. Fires one hint for this player (at most once per session). */
	void FireHint(FName HintId);

	/** SERVER. Fires every Manual hint with this TriggerKey. */
	void FireKey(FName Key);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
	/** Owner only: show this hint (if the profile has not seen it). */
	UFUNCTION(Client, Reliable)
	void ClientShowHint(FName HintId);

private:
	void Poll();
	void HandleCombatIdentityAggro(UBH_CombatIdentityComponent* Source, AActor* NewTarget);
	void HandleEnemyAggro(ABH_EnemyBase* Enemy, AActor* Target);
	void HandleEnemyEngaged(EBH_EnemyRank Rank, AActor* Target);

	APawn* GetLivingPawn() const;
	bool HasNotFired(const UBH_HintData* Hint) const;

	/** Hints already sent to the client this session. */
	TSet<FName> Fired;
	TWeakObjectPtr<const APawn> LastPawn;
	double PawnAliveSince = 0.0;

	FTimerHandle PollTimer;
	FDelegateHandle IdentityAggroHandle;
	FDelegateHandle EnemyAggroHandle;
};
