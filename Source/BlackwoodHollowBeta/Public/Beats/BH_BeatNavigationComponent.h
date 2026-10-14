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
//    is not net-relevant), with a marker tinted from the 13A palette: alive = Ally, dead and waiting for the party = Crimson,
//    gone for good (choice made / waiting for a revive) = Muted.
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
	};

	struct FPartnerProxy
	{
		TWeakObjectPtr<AActor> Actor;
		TWeakObjectPtr<UNavigationMarkerComponent> Marker;
		FVector TargetLocation = FVector::ZeroVector;
		int32 Phase = -1;
	};

	void Refresh();
	void RefreshObjective(const APlayerController* PlayerController, UBH_BeatTrackerComponent* Tracker);
	void RefreshPartners(const APlayerController* PlayerController, const UBH_BeatTrackerComponent* Tracker);
	UNavigationMarkerComponent* CreateMarker(AActor* Owner, const FText& Name, const FLinearColor& Tint) const;
	FLinearColor GetPaletteColor(FName Token, const FLinearColor& Fallback) const;
	FLinearColor GetPartnerColor(uint8 DeathPhase) const;
	void DestroyAllMarkers();

	TMap<TWeakObjectPtr<ABH_BeatTarget>, FTargetMarker> TargetMarkers;
	TMap<TWeakObjectPtr<APlayerState>, FPartnerProxy> Partners;

	FText LastObjectiveText;
	float TimeSinceRefresh = 0.f;
};
