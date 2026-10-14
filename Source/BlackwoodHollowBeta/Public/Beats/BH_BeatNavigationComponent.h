// Blackwood Hollow - local Narrative Navigator driver for the beat objective and the partners (Phase 13B+)
// Target: Unreal Engine 5.8 (C++)
//
// Narrative Navigator shows whatever UNavigationMarkerComponents are registered with the LOCAL player controller's
// UNarrativeNavigationComponent (markers register in their own BeginPlay, local controller only). This component, on ABH_GameState, is
// the bridge between the replicated beat state (UBH_BeatTrackerComponent) and Navigator. It never replicates and does nothing on a
// dedicated server, so each machine builds its own markers from the replicated state:
//
//  * NAV COMPONENT: PlayerController_BlackwoodHollow has no UNarrativeNavigationComponent, so EnsureNavigationComponent adds one at runtime
//    (not replicated). The HUD subsystem calls it before the HUD (and so the compass widget, which looks the component up in
//    OnInitialized) is created.
//  * OBJECTIVE MARKER: every placed ABH_BeatTarget gets a marker component. Only the current beat's target (gate open) is in the Compass
//    domain; if the row wants a world marker it is also in the ScreenSpace domain while the viewer is farther than
//    UBH_BeatTrackerComponent::WorldMarkerHideDistance. Show/hide = SetDomain (an empty domain shows nowhere).
//  * PARTNER MARKERS: one small local proxy actor per other player, moved to the replicated FBH_PartnerPing location (a far partner's pawn
//    is not net-relevant). The marker icon is chosen by the partner's replicated ABH_PlayerState::PlayerSlot (P1 Blue, P2 Yellow, P3 Green,
//    P4 Red; same on every machine) and the state shows by look: alive = solid, downed = pulsing, dead / waiting for a revive = grey at 40%.
//    The current-beat objective uses the YellowQuestMarker icon.
//  * The stock compass fades markers out beyond 150 m; the fade distances of our markers are pushed out to the whole island.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BH_BeatNavigationComponent.generated.h"

class AActor;
class APlayerController;
class APlayerState;
class ABH_BeatTarget;
class UBH_BeatTrackerComponent;
class UNarrativeNavigationComponent;
class UNavigationMarkerComponent;
class UTexture2D;

UCLASS(ClassGroup = "BlackwoodHollow")
class BLACKWOODHOLLOWBETA_API UBH_BeatNavigationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBH_BeatNavigationComponent();

	/** Finds or adds (not replicated) the Narrative Navigator component on a LOCAL player controller. Safe to call repeatedly. */
	UFUNCTION(BlueprintCallable, Category = "Beat")
	static UNarrativeNavigationComponent* EnsureNavigationComponent(APlayerController* PlayerController);

	/** Seconds between marker refreshes (partner proxies move every frame). */
	UPROPERTY(EditDefaultsOnly, Category = "Beat|Navigation", meta = (ClampMin = "0.05", ForceUnits = "s"))
	float RefreshInterval = 0.2f;

	/** How fast a partner proxy follows its 4 Hz position samples (1/s). */
	UPROPERTY(EditDefaultsOnly, Category = "Beat|Navigation", meta = (ClampMin = "1"))
	float PartnerSmoothing = 8.f;

	/** Navigator's compass starts fading a marker at this distance (cm). The stock 13500 would hide anything past 150 m. */
	UPROPERTY(EditDefaultsOnly, Category = "Beat|Navigation", meta = (ForceUnits = "cm"))
	float MarkerFadeStartDistance = 1000000.f;

	/** ... and the marker is gone at this distance (cm). */
	UPROPERTY(EditDefaultsOnly, Category = "Beat|Navigation", meta = (ForceUnits = "cm"))
	float MarkerFadeEndDistance = 1100000.f;

	/** The world marker shows again once the viewer is this much (cm) beyond the hide distance (stops flicker at the edge). */
	UPROPERTY(EditDefaultsOnly, Category = "Beat|Navigation", meta = (ClampMin = "0", ForceUnits = "cm"))
	float WorldMarkerHysteresis = 200.f;

	/** The 13A palette data asset the marker colours are read from. */
	UPROPERTY(EditDefaultsOnly, Category = "Beat|Navigation")
	FSoftObjectPath PalettePath;

	/** Icon of the current-beat objective marker (Compass/Markers/YellowQuestMarker). */
	UPROPERTY(EditDefaultsOnly, Category = "Beat|Navigation")
	TSoftObjectPtr<UTexture2D> ObjectiveIcon;

	/** Partner marker icon per player slot (index 0 = slot 1): Blue, Yellow, Green, Red. */
	UPROPERTY(EditDefaultsOnly, Category = "Beat|Navigation")
	TArray<TSoftObjectPtr<UTexture2D>> SlotIcons;

	/** Pulse speed (rad/s) of a downed partner's marker. */
	UPROPERTY(EditDefaultsOnly, Category = "Beat|Navigation", meta = (ClampMin = "0.1"))
	float DownedPulseSpeed = 6.f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	struct FTargetMarker
	{
		TWeakObjectPtr<UNavigationMarkerComponent> Marker;
		bool bOnCompass = false;
		bool bOnScreen = false;
		/** The text the marker was last registered with (the widget is built at registration). */
		FText DisplayText;
	};

	struct FPartnerProxy
	{
		TWeakObjectPtr<AActor> Actor;
		TWeakObjectPtr<UNavigationMarkerComponent> Marker;
		FVector TargetLocation = FVector::ZeroVector;
		int32 Phase = -1;
		/** The PlayerSlot the marker was last built for (0 = not known yet). */
		int32 Slot = 0;
	};

	void Refresh();
	void RefreshObjective(const APlayerController* PlayerController, UBH_BeatTrackerComponent* Tracker);
	void RefreshPartners(const APlayerController* PlayerController, const UBH_BeatTrackerComponent* Tracker);
	UNavigationMarkerComponent* CreateMarker(AActor* Owner, const FText& Name, const FLinearColor& Tint, const TSoftObjectPtr<UTexture2D>& Icon) const;
	FLinearColor GetPaletteColor(FName Token, const FLinearColor& Fallback) const;
	/** The palette colour of a player slot (tokens PlayerSlot1..4). */
	FLinearColor GetSlotColor(int32 Slot) const;
	/** Marker tint for a partner: alive = solid, downed (phase 1) = pulsing, dead or waiting for a revive (phase 2+) = grey at 40%. */
	FLinearColor GetPartnerTint(int32 Slot, uint8 DeathPhase) const;
	TSoftObjectPtr<UTexture2D> GetSlotIcon(int32 Slot) const;
	/** Sets the icon and tint for the partner's slot and state, and refreshes the marker. */
	void ApplyPartnerLook(FPartnerProxy& Proxy, int32 Slot, uint8 DeathPhase);
	void DestroyAllMarkers();

	TMap<TWeakObjectPtr<ABH_BeatTarget>, FTargetMarker> TargetMarkers;
	TMap<TWeakObjectPtr<APlayerState>, FPartnerProxy> Partners;

	FText LastObjectiveText;
	float TimeSinceRefresh = 0.f;
	float PulseClock = 0.f;
	float TimeSincePulseApply = 0.f;
};
