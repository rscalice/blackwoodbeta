// Blackwood Hollow - the Island 1 weapon cache (implementation)

#include "Actors/BH_WeaponCache.h"
#include "Combat/BH_LoadoutComponent.h"
#include "Interaction/BH_InteractorComponent.h"
#include "Items/BH_WeaponItem.h"
#include "Loot/BH_LootLibrary.h"
#include "NarrativeItem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogBHWeaponCache, Log, All);

ABH_WeaponCache::ABH_WeaponCache()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetNetUpdateFrequency(5.f);

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	SetRootComponent(BodyMesh);
	BodyMesh->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	BodyMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore); // the camera must not be shoved by a crate

	Interactable = CreateDefaultSubobject<UBH_InteractableComponent>(TEXT("Interactable"));
	Interactable->PromptName = NSLOCTEXT("BlackwoodHollow", "WeaponCacheName", "Weapon Cache");
	Interactable->PromptAction = NSLOCTEXT("BlackwoodHollow", "WeaponCacheAction", "Take weapon");
	Interactable->HoldSeconds = 0.f;
	Interactable->InteractRange = 250.f;
}

void ABH_WeaponCache::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABH_WeaponCache, TakenKeys);
}

bool ABH_WeaponCache::HasPlayerTaken(const APlayerState* PlayerState) const
{
	return PlayerState && TakenKeys.Contains(UBH_LootLibrary::GetPlayerKey(PlayerState));
}

bool ABH_WeaponCache::BH_CanInteract(const APawn* InteractingPawn, FText& OutDenyReason) const
{
	OutDenyReason = FText::GetEmpty();
	// Already taken by this player: not offered at all (empty reason hides the prompt).
	return InteractingPawn && InteractingPawn->GetPlayerState() && !HasPlayerTaken(InteractingPawn->GetPlayerState());
}

void ABH_WeaponCache::BH_OnInteractionCompleted(APawn* InteractingPawn)
{
	if (!HasAuthority() || !InteractingPawn)
	{
		return;
	}
	APlayerState* TakerState = InteractingPawn->GetPlayerState();
	if (!TakerState || HasPlayerTaken(TakerState))
	{
		return;
	}

	UBH_LoadoutComponent* Loadout = UBH_LoadoutComponent::FindLoadoutComponent(InteractingPawn);
	UClass* WeaponClass = WeaponItemClass.LoadSynchronous();
	if (!Loadout || !WeaponClass)
	{
		UE_LOG(LogBHWeaponCache, Warning, TEXT("%s: nothing granted to %s (loadout component: %s, weapon class: %s)."), *GetNameSafe(this), *GetNameSafe(InteractingPawn),
			Loadout ? TEXT("yes") : TEXT("missing"), *WeaponItemClass.ToString());
		return;
	}

	// Grant (or reuse) the weapon and equip it into EquipSlot; the preset also switches the stance to it.
	FBH_StarterLoadoutEntry Entry;
	Entry.ItemClass = WeaponItemClass;
	Entry.bEquip = true;
	Entry.Slot = EquipSlot;
	TArray<FBH_StarterLoadoutEntry> Preset;
	Preset.Add(Entry);

	FText Reason;
	if (!Loadout->ApplyLoadoutPreset(Preset, Reason))
	{
		// Not spent: the player can try again (a full inventory, for example).
		UE_LOG(LogBHWeaponCache, Warning, TEXT("%s: %s could not take the weapon: %s"), *GetNameSafe(this), *GetNameSafe(InteractingPawn), *Reason.ToString());
		return;
	}

	TakenKeys.Add(UBH_LootLibrary::GetPlayerKey(TakerState));
	ForceNetUpdate();
	OnRep_TakenKeys(); // the server does not get the RepNotify

	if (UBH_InteractorComponent* Interactor = UBH_InteractorComponent::Find(InteractingPawn))
	{
		const TSubclassOf<UNarrativeItem> GrantedClass(WeaponClass);
		Interactor->NotifyItemGranted(UBH_LootLibrary::GetItemDisplayName(GrantedClass), 1);
	}
	UE_LOG(LogBHWeaponCache, Log, TEXT("%s: %s took %s."), *GetNameSafe(this), *GetNameSafe(TakerState), *GetNameSafe(WeaponClass));
}

void ABH_WeaponCache::BH_DebugReset()
{
	if (!HasAuthority())
	{
		return;
	}
	TakenKeys.Reset();
	ForceNetUpdate();
	OnRep_TakenKeys();
}

void ABH_WeaponCache::OnRep_TakenKeys()
{
	BP_OnTakenChanged();
}
