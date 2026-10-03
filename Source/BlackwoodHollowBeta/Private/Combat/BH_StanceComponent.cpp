// Blackwood Hollow - replicated weapon stance component (implementation)

#include "Combat/BH_StanceComponent.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystem/BH_CombatFunctionLibrary.h"
#include "Combat/BH_LoadoutComponent.h"
#include "Combat/BH_WeaponLoadoutDataAsset.h"
#include "Characters/BH_StanceMovementProfile.h"
#include "UI/BH_HUDWidget.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "Components/MeshComponent.h"
#include "GameFramework/Character.h"
#include "Net/UnrealNetwork.h"

UBH_StanceComponent::UBH_StanceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	DefaultStance = TAG_Stance_Weapon_Unarmed.GetTag();
}

void UBH_StanceComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UBH_StanceComponent, CurrentStance);
}

void UBH_StanceComponent::BeginPlay()
{
	Super::BeginPlay();

	const AActor* Owner = GetOwner();
	if (Owner && Owner->HasAuthority() && !CurrentStance.IsValid())
	{
		SetStance(DefaultStance); // applies locally
	}
	else if (CurrentStance.IsValid())
	{
		ApplyStanceLocal(FGameplayTag(), CurrentStance);
	}
}

UBH_StanceMovementProfile* UBH_StanceComponent::GetActiveMovementProfile() const
{
	const TObjectPtr<UBH_StanceMovementProfile>* Found = MovementProfiles.Find(CurrentStance);
	return Found && *Found ? Found->Get() : DefaultMovementProfile.Get();
}

FName UBH_StanceComponent::GetCurrentStanceLegacyName() const
{
	return BH_Stance::ToLegacyName(CurrentStance);
}

bool UBH_StanceComponent::IsStanceAllowed(FGameplayTag Stance) const
{
	if (!BH_Stance::IsWeaponStance(Stance))
	{
		return false;
	}
	const UBH_LoadoutComponent* Loadout = GetOwner() ? GetOwner()->FindComponentByClass<UBH_LoadoutComponent>() : nullptr;
	if (!Loadout || Loadout->AvailableStances.Num() == 0)
	{
		return true;
	}
	return Stance == TAG_Stance_Weapon_Unarmed.GetTag() || Loadout->AvailableStances.Contains(BH_Stance::ToLegacyName(Stance));
}

UBH_StanceComponent* UBH_StanceComponent::FindStanceComponent(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UBH_StanceComponent>() : nullptr;
}

FGameplayTag UBH_StanceComponent::GetStanceTagOf(const AActor* Actor)
{
	const UBH_StanceComponent* Stance = FindStanceComponent(Actor);
	return Stance && Stance->CurrentStance.IsValid() ? Stance->CurrentStance : TAG_Stance_Weapon_Unarmed.GetTag();
}

bool UBH_StanceComponent::RequestStance(FGameplayTag NewStance)
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}
	if (Owner->HasAuthority())
	{
		return SetStance(NewStance);
	}
	const APawn* Pawn = Cast<APawn>(Owner);
	if (Pawn && Pawn->IsLocallyControlled())
	{
		ServerSetStance(NewStance);
		return true;
	}
	return false;
}

bool UBH_StanceComponent::RequestStanceByLegacyName(FName LegacyName)
{
	const FGameplayTag Stance = BH_Stance::FromLegacyName(LegacyName);
	return Stance.IsValid() && RequestStance(Stance);
}

bool UBH_StanceComponent::SetStance(FGameplayTag NewStance)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return false;
	}
	if (NewStance == CurrentStance)
	{
		return true;
	}
	if (!IsStanceAllowed(NewStance))
	{
		UE_LOG(LogBHCombat, Verbose, TEXT("%s: stance '%s' rejected."), *GetNameSafe(Owner), *NewStance.ToString());
		return false;
	}

	const FGameplayTag Old = CurrentStance;
	CurrentStance = NewStance;
	ApplyStanceLocal(Old, NewStance);
	Owner->ForceNetUpdate();
	return true;
}

void UBH_StanceComponent::ServerSetStance_Implementation(FGameplayTag NewStance)
{
	SetStance(NewStance);
}

bool UBH_StanceComponent::ServerSetStance_Validate(FGameplayTag NewStance)
{
	return true; // bad input is dropped by SetStance, never a kick
}

void UBH_StanceComponent::OnRep_CurrentStance(FGameplayTag OldStance)
{
	ApplyStanceLocal(OldStance, CurrentStance);
}

void UBH_StanceComponent::ApplyStanceLocal(FGameplayTag Old, FGameplayTag New)
{
	MirrorLooseTag(Old, New);
	EquipWeaponsFor(New);
	UBH_HUDWidget::BroadcastStanceChanged(GetOwner(), BH_Stance::ToLegacyName(New).ToString());
	OnStanceChanged.Broadcast(Old, New);
}

void UBH_StanceComponent::MirrorLooseTag(FGameplayTag Old, FGameplayTag New) const
{
	UAbilitySystemComponent* ASC = bMirrorStanceAsLooseTag ? ResolveASC() : nullptr;
	if (!ASC)
	{
		return;
	}
	if (Old.IsValid() && Old != New)
	{
		ASC->SetLooseGameplayTagCount(Old, 0);
	}
	if (New.IsValid())
	{
		ASC->SetLooseGameplayTagCount(New, 1);
	}
}

void UBH_StanceComponent::EquipWeaponsFor(FGameplayTag New) const
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character || !WeaponLoadouts)
	{
		return;
	}
	TArray<UMeshComponent*> Attached;
	UBH_CombatFunctionLibrary::EquipWeaponsForOverlayPose(Character, WeaponLoadouts, BH_Stance::ToLegacyName(New).ToString(), Attached);
}

UAbilitySystemComponent* UBH_StanceComponent::ResolveASC() const
{
	AActor* Owner = GetOwner();
	if (const IAbilitySystemInterface* Interface = Cast<IAbilitySystemInterface>(Owner))
	{
		return Interface->GetAbilitySystemComponent();
	}
	return Owner ? Owner->FindComponentByClass<UAbilitySystemComponent>() : nullptr;
}
