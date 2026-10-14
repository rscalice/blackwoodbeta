// Blackwood Hollow - local Narrative Navigator driver (Phase 13B+, implementation)

#include "Beats/BH_BeatNavigationComponent.h"
#include "Beats/BH_BeatTarget.h"
#include "Beats/BH_BeatTrackerComponent.h"
#include "Beats/BH_BeatTypes.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SceneComponent.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameplayTagContainer.h"
#include "NarrativeNavigationComponent.h"
#include "NavigationMarkerComponent.h"
#include "Player/BH_PlayerState.h"

namespace
{
	const FName CompassDomainTag(TEXT("Navigator.NavigatorTypes.Compass"));
	const FName ScreenSpaceDomainTag(TEXT("Navigator.NavigatorTypes.ScreenSpace"));

	/** Navigator's fade distances are protected; the reflection system does not care. */
	void SetFloatProperty(UObject* Object, const FName PropertyName, const float Value)
	{
		if (!Object)
		{
			return;
		}
		if (FProperty* Property = Object->GetClass()->FindPropertyByName(PropertyName))
		{
			if (const FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
			{
				Numeric->SetFloatingPointPropertyValue(Property->ContainerPtrToValuePtr<void>(Object), static_cast<double>(Value));
			}
		}
	}

	FGameplayTagContainer MakeDomain(const bool bCompass, const bool bScreenSpace)
	{
		FGameplayTagContainer Domain;
		if (bCompass)
		{
			Domain.AddTag(FGameplayTag::RequestGameplayTag(CompassDomainTag, false));
		}
		if (bScreenSpace)
		{
			Domain.AddTag(FGameplayTag::RequestGameplayTag(ScreenSpaceDomainTag, false));
		}
		return Domain;
	}
}

UBH_BeatNavigationComponent::UBH_BeatNavigationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	SetIsReplicatedByDefault(false);
	PalettePath = FSoftObjectPath(TEXT("/Game/BlackwoodHollow/UI/Style/DA_UI_Palette.DA_UI_Palette"));
	ObjectiveIcon = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/BlackwoodHollow/UI/Compass/Markers/YellowQuestMarker.YellowQuestMarker")));
	// Slot 1 Blue, 2 Yellow, 3 Green, 4 Red.
	SlotIcons = {
		TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/BlackwoodHollow/UI/Compass/Markers/BlueMarker.BlueMarker"))),
		TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/BlackwoodHollow/UI/Compass/Markers/YellowMarker.YellowMarker"))),
		TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/BlackwoodHollow/UI/Compass/Markers/GreenMarker.GreenMarker"))),
		TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/BlackwoodHollow/UI/Compass/Markers/RedMarker.RedMarker")))
	};
}

UNarrativeNavigationComponent* UBH_BeatNavigationComponent::EnsureNavigationComponent(APlayerController* PlayerController)
{
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return nullptr;
	}
	if (UNarrativeNavigationComponent* Existing = PlayerController->FindComponentByClass<UNarrativeNavigationComponent>())
	{
		return Existing;
	}
	UNarrativeNavigationComponent* NavComp = NewObject<UNarrativeNavigationComponent>(PlayerController, TEXT("BH_NarrativeNavigation"), RF_Transient);
	// Built locally on every machine; it must not also arrive through replication.
	NavComp->SetIsReplicated(false);
	NavComp->RegisterComponent();
	UE_LOG(LogTemp, Log, TEXT("[BH_Beat] added Narrative Navigator component to %s"), *GetNameSafe(PlayerController));
	return NavComp;
}

void UBH_BeatNavigationComponent::BeginPlay()
{
	Super::BeginPlay();
	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		SetComponentTickEnabled(false);
	}
}

void UBH_BeatNavigationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DestroyAllMarkers();
	Super::EndPlay(EndPlayReason);
}

void UBH_BeatNavigationComponent::DestroyAllMarkers()
{
	for (TPair<TWeakObjectPtr<ABH_BeatTarget>, FTargetMarker>& Pair : TargetMarkers)
	{
		if (UNavigationMarkerComponent* Marker = Pair.Value.Marker.Get())
		{
			Marker->DestroyComponent();
		}
	}
	TargetMarkers.Reset();
	for (TPair<TWeakObjectPtr<APlayerState>, FPartnerProxy>& Pair : Partners)
	{
		if (AActor* Proxy = Pair.Value.Actor.Get())
		{
			Proxy->Destroy();
		}
	}
	Partners.Reset();
}

void UBH_BeatNavigationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Partner proxies glide toward their 4 Hz samples so the compass marker does not step.
	for (TPair<TWeakObjectPtr<APlayerState>, FPartnerProxy>& Pair : Partners)
	{
		if (AActor* Proxy = Pair.Value.Actor.Get())
		{
			Proxy->SetActorLocation(FMath::VInterpTo(Proxy->GetActorLocation(), Pair.Value.TargetLocation, DeltaTime, PartnerSmoothing));
		}
	}

	// A downed partner's marker pulses (the tint is re-applied about 20 times a second).
	PulseClock += DeltaTime;
	TimeSincePulseApply += DeltaTime;
	if (TimeSincePulseApply >= 0.05f)
	{
		TimeSincePulseApply = 0.f;
		for (TPair<TWeakObjectPtr<APlayerState>, FPartnerProxy>& Pair : Partners)
		{
			if (Pair.Value.Phase == 1)
			{
				if (UNavigationMarkerComponent* Marker = Pair.Value.Marker.Get())
				{
					Marker->SetIconTint(GetPartnerTint(Pair.Value.Slot, 1));
					Marker->RefreshMarker();
				}
			}
		}
	}

	TimeSinceRefresh += DeltaTime;
	if (TimeSinceRefresh >= RefreshInterval)
	{
		TimeSinceRefresh = 0.f;
		Refresh();
	}
}

void UBH_BeatNavigationComponent::Refresh()
{
	const UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return;
	}
	// The nav component must exist before any marker is registered (markers register once, in their own BeginPlay).
	if (!EnsureNavigationComponent(PlayerController))
	{
		return;
	}
	UBH_BeatTrackerComponent* Tracker = GetOwner() ? GetOwner()->FindComponentByClass<UBH_BeatTrackerComponent>() : nullptr;
	if (!Tracker)
	{
		return;
	}
	RefreshObjective(PlayerController, Tracker);
	RefreshPartners(PlayerController, Tracker);
}

FLinearColor UBH_BeatNavigationComponent::GetPaletteColor(const FName Token, const FLinearColor& Fallback) const
{
	if (UObject* Palette = PalettePath.TryLoad())
	{
		const FStructProperty* Property = CastField<FStructProperty>(Palette->GetClass()->FindPropertyByName(Token));
		if (Property && Property->Struct == TBaseStructure<FLinearColor>::Get())
		{
			return *Property->ContainerPtrToValuePtr<FLinearColor>(Palette);
		}
	}
	return Fallback;
}

FLinearColor UBH_BeatNavigationComponent::GetSlotColor(const int32 Slot) const
{
	// Fallbacks match the PlayerSlot1..4 tokens in the style guide: P1 Blue, P2 Yellow, P3 Green, P4 Red.
	static const TCHAR* const Hex[4] = { TEXT("5FA8E8"), TEXT("E8C46A"), TEXT("7FB77E"), TEXT("D9534F") };
	const int32 Index = FMath::Clamp(Slot, 1, 4) - 1;
	const FName Token(*FString::Printf(TEXT("PlayerSlot%d"), Index + 1));
	return GetPaletteColor(Token, FLinearColor::FromSRGBColor(FColor::FromHex(Hex[Index])));
}

TSoftObjectPtr<UTexture2D> UBH_BeatNavigationComponent::GetSlotIcon(const int32 Slot) const
{
	const int32 Index = FMath::Clamp(Slot, 1, 4) - 1;
	return SlotIcons.IsValidIndex(Index) ? SlotIcons[Index] : TSoftObjectPtr<UTexture2D>();
}

FLinearColor UBH_BeatNavigationComponent::GetPartnerTint(const int32 Slot, const uint8 DeathPhase) const
{
	// The slot icons are already coloured, so they are drawn untinted; without an icon the slot colour tints the stock marker.
	const FLinearColor Base = GetSlotIcon(Slot).IsNull() ? GetSlotColor(Slot) : FLinearColor::White;
	switch (DeathPhase)
	{
	case 0:
		// Alive: solid.
		return Base;
	case 1:
		{
			// Downed: pulsing.
			const float Pulse = 0.5f + 0.5f * FMath::Sin(PulseClock * DownedPulseSpeed);
			return FLinearColor(Base.R, Base.G, Base.B, FMath::Lerp(0.35f, 1.f, Pulse));
		}
	default:
		// Dead or waiting for a revive: greyed, 40% opacity.
		return FLinearColor(Base.R * 0.45f, Base.G * 0.45f, Base.B * 0.45f, 0.4f);
	}
}

void UBH_BeatNavigationComponent::ApplyPartnerLook(FPartnerProxy& Proxy, const int32 Slot, const uint8 DeathPhase)
{
	UNavigationMarkerComponent* Marker = Proxy.Marker.Get();
	if (!Marker)
	{
		return;
	}
	Proxy.Slot = Slot;
	Proxy.Phase = DeathPhase;
	const TSoftObjectPtr<UTexture2D> Icon = GetSlotIcon(Slot);
	if (!Icon.IsNull())
	{
		Marker->DefaultMarkerSettings.bOverride_LocationIcon = true;
		Marker->DefaultMarkerSettings.LocationIcon = Icon;
	}
	Marker->DefaultMarkerSettings.bOverride_IconTint = true;
	Marker->SetIconTint(GetPartnerTint(Slot, DeathPhase));
	Marker->RefreshMarker();
}
UNavigationMarkerComponent* UBH_BeatNavigationComponent::CreateMarker(AActor* Owner, const FText& Name, const FLinearColor& Tint, const TSoftObjectPtr<UTexture2D>& Icon) const
{
	UNavigationMarkerComponent* Marker = NewObject<UNavigationMarkerComponent>(Owner, NAME_None, RF_Transient);
	Marker->DefaultMarkerSettings.LocationDisplayName = Name;
	if (!Icon.IsNull())
	{
		Marker->DefaultMarkerSettings.bOverride_LocationIcon = true;
		Marker->DefaultMarkerSettings.LocationIcon = Icon;
	}
	Marker->DefaultMarkerSettings.bOverride_IconTint = true;
	Marker->DefaultMarkerSettings.IconTint = Tint;
	SetFloatProperty(Marker, TEXT("MarkerStartFadeOutDistance"), MarkerFadeStartDistance);
	SetFloatProperty(Marker, TEXT("MarkerStartFadeInDistance"), MarkerFadeEndDistance);
	// Registers with the local Navigator component in its BeginPlay.
	Marker->RegisterComponent();
	return Marker;
}

void UBH_BeatNavigationComponent::RefreshObjective(const APlayerController* PlayerController, UBH_BeatTrackerComponent* Tracker)
{
	UWorld* World = GetWorld();
	FBH_ObjectiveInfo Info;
	const bool bHasObjective = Tracker->GetCurrentObjective(Info);

	FVector Viewer = FVector::ZeroVector;
	if (PlayerController->PlayerCameraManager)
	{
		Viewer = PlayerController->PlayerCameraManager->GetCameraLocation();
	}
	else if (const APawn* Pawn = PlayerController->GetPawn())
	{
		Viewer = Pawn->GetActorLocation();
	}

	for (TActorIterator<ABH_BeatTarget> It(World); It; ++It)
	{
		ABH_BeatTarget* Target = *It;
		FTargetMarker& Entry = TargetMarkers.FindOrAdd(Target);
		if (!Entry.Marker.IsValid())
		{
			// The quest icon is already coloured (yellow), so it is drawn untinted; without it the stock marker is tinted Bone.
			Entry.Marker = CreateMarker(Target, Info.ObjectiveText, ObjectiveIcon.IsNull() ? GetPaletteColor(TEXT("Bone"), FLinearColor(0.791f, 0.716f, 0.578f)) : FLinearColor::White, ObjectiveIcon);
			if (UNavigationMarkerComponent* Created = Entry.Marker.Get())
			{
				Created->SetDomain(MakeDomain(false, false));
			}
			Entry.bOnCompass = false;
			Entry.bOnScreen = false;
		}
		UNavigationMarkerComponent* Marker = Entry.Marker.Get();
		if (!Marker)
		{
			continue;
		}

		// This target is the active objective when its position is the one the tracker resolved, and the gate is open.
		const bool bIsCurrent = bHasObjective && Info.bHasTarget && Info.bGateOpen && FVector::DistSquared(Target->GetActorLocation(), Info.TargetLocation) < 1.0;

		bool bWantScreen = false;
		if (bIsCurrent && Info.bShowWorldMarker)
		{
			const double Distance = FVector::Dist(Viewer, Target->GetActorLocation());
			const double Limit = Tracker->WorldMarkerHideDistance + (Entry.bOnScreen ? 0.0 : WorldMarkerHysteresis);
			bWantScreen = Distance > Limit;
		}

		const bool bTextChanged = bIsCurrent && !Entry.DisplayText.EqualTo(Info.ObjectiveText);
		if (bIsCurrent != Entry.bOnCompass || bWantScreen != Entry.bOnScreen || bTextChanged)
		{
			if (bIsCurrent)
			{
				// Name first: the Navigator builds the marker widgets at (re-)registration, so the text must already be right.
				Marker->SetLocationDisplayName(Info.ObjectiveText);
				Entry.DisplayText = Info.ObjectiveText;
			}
			Marker->SetDomain(MakeDomain(bIsCurrent, bWantScreen));
			Entry.bOnCompass = bIsCurrent;
			Entry.bOnScreen = bWantScreen;
		}
	}
	if (bHasObjective)
	{
		LastObjectiveText = Info.ObjectiveText;
	}
}

void UBH_BeatNavigationComponent::RefreshPartners(const APlayerController* PlayerController, const UBH_BeatTrackerComponent* Tracker)
{
	UWorld* World = GetWorld();
	const APlayerState* LocalPlayerState = PlayerController->PlayerState;
	TSet<TWeakObjectPtr<APlayerState>> Seen;

	for (const FBH_PartnerPing& Ping : Tracker->GetPartnerPings())
	{
		APlayerState* Player = Ping.Player;
		if (!Player || Player == LocalPlayerState)
		{
			continue;
		}
		Seen.Add(Player);
		FPartnerProxy& Proxy = Partners.FindOrAdd(Player);
		const FVector Location(Ping.Location);
		Proxy.TargetLocation = Location;

		if (!Proxy.Actor.IsValid())
		{
			FActorSpawnParameters Params;
			Params.ObjectFlags |= RF_Transient;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AActor* Spawned = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Location), Params);
			if (!Spawned)
			{
				continue;
			}
			USceneComponent* Root = NewObject<USceneComponent>(Spawned, TEXT("Root"), RF_Transient);
			Spawned->SetRootComponent(Root);
			Root->RegisterComponent();
			Spawned->SetActorLocation(Location);
			Spawned->SetActorEnableCollision(false);
			Proxy.Actor = Spawned;
			// PlayerSlot replicates with the PlayerState; 0 until it arrives (the look is refreshed once it does).
			const ABH_PlayerState* BHPlayer = Cast<ABH_PlayerState>(Player);
			const int32 Slot = BHPlayer ? BHPlayer->GetPlayerSlot() : 0;
			Proxy.Slot = Slot;
			Proxy.Phase = Ping.DeathPhase;
			Proxy.Marker = CreateMarker(Spawned, FText::FromString(Player->GetPlayerName()), GetPartnerTint(Slot, Ping.DeathPhase), GetSlotIcon(Slot));
		}
		else
		{
			const ABH_PlayerState* BHPlayer = Cast<ABH_PlayerState>(Player);
			const int32 Slot = BHPlayer ? BHPlayer->GetPlayerSlot() : 0;
			if (Proxy.Phase != static_cast<int32>(Ping.DeathPhase) || Proxy.Slot != Slot)
			{
				ApplyPartnerLook(Proxy, Slot, Ping.DeathPhase);
			}
		}
	}

	// Players who left.
	for (auto It = Partners.CreateIterator(); It; ++It)
	{
		if (!Seen.Contains(It.Key()))
		{
			if (AActor* Proxy = It.Value().Actor.Get())
			{
				Proxy->Destroy();
			}
			It.RemoveCurrent();
		}
	}
}
