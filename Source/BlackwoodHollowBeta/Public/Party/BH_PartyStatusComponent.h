// Blackwood Hollow - party status component (Phase 14A-3 prep)
// Target: Unreal Engine 5.8 (C++)
//
// Lives on ABH_GameState (one party per session), so every machine has it and it replicates to late joiners.
//
//  * PARTNER PINGS: the server samples every player pawn's location and death phase at about 4 Hz into a replicated array (a far partner's pawn
//    is not net-relevant, so a client could not read it directly). The compass reads GetPartnerPings. Moved here from UBH_BeatTrackerComponent so
//    the beat tracker can be deleted in 14A-3.
//
// OnPartnersChanged fires on every machine when the list changed. Nothing here knows about widgets.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Party/BH_PartyStatusTypes.h"
#include "BH_PartyStatusComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FBH_OnPartnersChanged);

UCLASS(ClassGroup = "BlackwoodHollow", meta = (BlueprintSpawnableComponent))
class BLACKWOODHOLLOWBETA_API UBH_PartyStatusComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBH_PartyStatusComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Seconds between partner samples on the server. */
	UPROPERTY(EditDefaultsOnly, Category = "Party", meta = (ClampMin = "0.05", ForceUnits = "s"))
	float PartnerUpdateInterval = 0.25f;

	/** Any machine. The party status of Context's world, or null (no game state yet). */
	UFUNCTION(BlueprintPure, Category = "Party", meta = (WorldContext = "WorldContext"))
	static UBH_PartyStatusComponent* Get(const UObject* WorldContext);

	/** Any machine. Every player as last sampled by the server (includes the local player; filter it out in the widget). */
	UFUNCTION(BlueprintPure, Category = "Party")
	const TArray<FBH_PartnerPing>& GetPartnerPings() const { return PartnerPings; }

	/** Pure: true when A and B list the same players in the same order with the same death phase and locations within 50 cm. */
	static bool ArePingListsEqual(const TArray<FBH_PartnerPing>& A, const TArray<FBH_PartnerPing>& B);

	/** Any machine, when the partner list changed. */
	UPROPERTY(BlueprintAssignable, Category = "Party")
	FBH_OnPartnersChanged OnPartnersChanged;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(ReplicatedUsing = OnRep_PartnerPings)
	TArray<FBH_PartnerPing> PartnerPings;

	UFUNCTION()
	void OnRep_PartnerPings();

private:
	void UpdatePartnerPings();

	FTimerHandle PartnerTimer;
};
