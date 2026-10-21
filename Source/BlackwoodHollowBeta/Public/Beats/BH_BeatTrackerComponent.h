// Blackwood Hollow - party beat tracker (Phase 13B+)
// Target: Unreal Engine 5.8 (C++)
//
// Lives on ABH_GameState (one party per session), so every machine has it and it replicates to late joiners.
//
//  * CURRENT BEAT: a replicated Beat.Island1.* tag. The SERVER owns it. It moves forward only: CompleteBeat(X) makes the beat AFTER X current
//    when X is the current beat or a later one (skipping ahead is fine, a player who walks past a beat has finished it); completing an earlier
//    beat does nothing. Beats end three ways: an ABH_BeatTrigger volume, a world flag (FBH_BeatRow::CompleteWorldFlag, watched on
//    ABH_GameState::OnWorldFlagChanged, covering the barricade and the Span Gate), or a call from Blueprint / Phase 14 quests.
//  * DATA: DT_BH_Beats (FBH_BeatRow), one row per beat IN ORDER.
//  * OBJECTIVE: GetCurrentObjective gives the text, the target location (an ABH_BeatTarget found by tag, cached) and whether the gate is open.
//    While the gate flag is unset the direction is withheld, so the marker never points through a locked gate.
//  * PARTNERS: the server samples every player pawn's location and death phase at about 4 Hz into a replicated array (a far partner's pawn
//    is not net-relevant, so a client could not read it directly). The compass reads GetPartnerPings.
//
// The UI is local per player: nothing here knows about widgets. OnBeatChanged / OnObjectiveChanged / OnPartnersChanged fire on every machine.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Beats/BH_BeatTypes.h"
#include "BH_BeatTrackerComponent.generated.h"

class UDataTable;
class ABH_GameState;
class ABH_BeatTarget;
class APlayerState;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FBH_OnBeatChanged, FGameplayTag, NewBeat, FGameplayTag, OldBeat);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FBH_OnObjectiveChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FBH_OnPartnersChanged);

UCLASS(ClassGroup = "BlackwoodHollow", meta = (BlueprintSpawnableComponent))
class BLACKWOODHOLLOWBETA_API UBH_BeatTrackerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBH_BeatTrackerComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** The beat table (FBH_BeatRow). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Beat")
	TSoftObjectPtr<UDataTable> BeatTable;

	/** The in-world marker hides inside this distance of the viewer (cm). 1500 = 15 m. Recorded in claude/balance.md. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Beat", meta = (ClampMin = "0", ForceUnits = "cm"))
	float WorldMarkerHideDistance = 1500.f;

	/** Seconds between partner samples on the server. */
	UPROPERTY(EditDefaultsOnly, Category = "Beat", meta = (ClampMin = "0.05", ForceUnits = "s"))
	float PartnerUpdateInterval = 0.25f;

	/** Any machine. The tracker of Context's world, or null (no game state yet). */
	UFUNCTION(BlueprintPure, Category = "Beat", meta = (WorldContext = "WorldContext"))
	static UBH_BeatTrackerComponent* Get(const UObject* WorldContext);

	/** Any machine. The replicated current beat tag (empty before the first beat is set). */
	UFUNCTION(BlueprintPure, Category = "Beat")
	FGameplayTag GetCurrentBeat() const { return CurrentBeat; }

	/** Any machine. 0-based index of a beat in the table, or INDEX_NONE. */
	UFUNCTION(BlueprintPure, Category = "Beat")
	int32 GetBeatIndex(FGameplayTag Beat) const;

	/** SERVER. See the class comment: moves the beat to the one after Beat, forward only. Returns true if the current beat changed. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Beat")
	bool CompleteBeat(FGameplayTag Beat);

	/** SERVER. Forces the current beat (debug / Phase 14). Any tag in the table, or Beat.Island1.Complete. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Beat")
	void SetCurrentBeat(FGameplayTag Beat);

	/** Convenience for volumes and Blueprints: finds the tracker and calls CompleteBeat. Does nothing on a client. */
	UFUNCTION(BlueprintCallable, Category = "Beat", meta = (WorldContext = "WorldContext"))
	static bool CompleteBeatOnServer(const UObject* WorldContext, FGameplayTag Beat);

	/** Any machine. Fills Out for the current beat. False when there is no active beat (before the table loads, or all beats done). */
	UFUNCTION(BlueprintCallable, Category = "Beat")
	bool GetCurrentObjective(FBH_ObjectiveInfo& Out);

	/** Any machine. True when the in-world marker should show for a viewer at ViewerLocation: gate open, row allows it, and farther than WorldMarkerHideDistance. */
	UFUNCTION(BlueprintCallable, Category = "Beat")
	bool ShouldShowWorldMarker(const FVector& ViewerLocation);

	// -- Scavenging checklist (a beat whose targets carry a ChecklistItemId) -----------------------------------------------
	// Beat progress stays PARTY-WIDE (one replicated current beat). The checklist is per player: the server keeps "<player key>|<item id>"
	// entries (replicated, so every machine knows who did what, also for targets/containers that are not loaded on that machine), the markers
	// and the counter are computed for the LOCAL player, and the beat advances as soon as ANY one player has done every item of it.

	/** SERVER. Records that Player did checklist item ItemId (a container opened, the cache taken) and completes the beat if that finishes the list. */
	UFUNCTION(BlueprintCallable, Category = "Beat|Checklist", meta = (WorldContext = "WorldContext"))
	static void ReportChecklistItem(const UObject* WorldContext, APlayerState* Player, FName ItemId);

	/** SERVER, debug (bh.Loot.ResetContainers): forgets ItemId for every player. */
	UFUNCTION(BlueprintCallable, Category = "Beat|Checklist", meta = (WorldContext = "WorldContext"))
	static void ResetChecklistItem(const UObject* WorldContext, FName ItemId);

	/** Any machine. True when Player has done checklist item ItemId. */
	UFUNCTION(BlueprintPure, Category = "Beat|Checklist")
	bool IsChecklistItemDone(const APlayerState* Player, FName ItemId) const;

	/** Any machine. Items done / total for the current beat. False when the current beat has no checklist. */
	UFUNCTION(BlueprintCallable, Category = "Beat|Checklist")
	bool GetChecklistProgress(const APlayerState* Player, int32& OutDone, int32& OutTotal);

	/** Any machine. True when Target is a marker of the current beat that the LOCAL player still has to visit (every target of the beat for a plain beat). */
	UFUNCTION(BlueprintCallable, Category = "Beat|Checklist")
	bool IsTargetActive(const ABH_BeatTarget* Target);

	/** Any machine. Every player as last sampled by the server (includes the local player; filter it out in the widget). */
	UFUNCTION(BlueprintPure, Category = "Beat")
	const TArray<FBH_PartnerPing>& GetPartnerPings() const { return PartnerPings; }

	/** Any machine, when CurrentBeat changed. */
	UPROPERTY(BlueprintAssignable, Category = "Beat")
	FBH_OnBeatChanged OnBeatChanged;

	/** Any machine, when the objective text, target or gate state may have changed (new beat, a world flag changed). */
	UPROPERTY(BlueprintAssignable, Category = "Beat")
	FBH_OnObjectiveChanged OnObjectiveChanged;

	/** Any machine, when the partner list changed. */
	UPROPERTY(BlueprintAssignable, Category = "Beat")
	FBH_OnPartnersChanged OnPartnersChanged;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(ReplicatedUsing = OnRep_CurrentBeat)
	FGameplayTag CurrentBeat;

	UPROPERTY(ReplicatedUsing = OnRep_PartnerPings)
	TArray<FBH_PartnerPing> PartnerPings;

	UFUNCTION()
	void OnRep_CurrentBeat(FGameplayTag OldBeat);

	UFUNCTION()
	void OnRep_PartnerPings();

	/** "<player key>|<item id>" per finished checklist item. */
	UPROPERTY(ReplicatedUsing = OnRep_ChecklistDone)
	TArray<FString> ChecklistDone;

	UFUNCTION()
	void OnRep_ChecklistDone();

	UFUNCTION()
	void HandleWorldFlagChanged(FName Flag, bool bIsSet);

private:
	void LoadRows();
	FGameplayTag GetCurrentTargetTag();
	const TArray<TWeakObjectPtr<ABH_BeatTarget>>& GetChecklistTargets();
	const APlayerState* GetLocalPlayerState() const;
	void EvaluateChecklistCompletion();
	void SetCurrentBeatInternal(FGameplayTag NewBeat);
	void CompleteBeatsForSetFlags();
	void UpdatePartnerPings();
	bool SamePings(const TArray<FBH_PartnerPing>& Other) const;
	AActor* ResolveTarget(const FBH_BeatRow& Row);
	const ABH_GameState* GetBeatGameState() const;

	/** Cached copy of the table rows, in table order. */
	TArray<FBH_BeatRow> Rows;
	bool bRowsLoaded = false;

	TArray<TWeakObjectPtr<ABH_BeatTarget>> ChecklistTargets;
	FGameplayTag ChecklistCacheTag;
	double ChecklistCacheTime = -100.0;

	TWeakObjectPtr<AActor> CachedTarget;
	double LastTargetSearchTime = -100.0;

	FTimerHandle PartnerTimer;
	bool bBoundFlags = false;
};