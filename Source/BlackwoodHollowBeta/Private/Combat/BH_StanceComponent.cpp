// Blackwood Hollow - replicated weapon stance component (implementation)

#include "Combat/BH_StanceComponent.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystem/BH_CombatFunctionLibrary.h"
#include "Combat/BH_LoadoutComponent.h"
#include "Combat/BH_LockOnComponent.h"
#include "Combat/BH_WeaponLoadoutDataAsset.h"
#include "Characters/BH_StanceMovementProfile.h"
#include "UI/BH_HUDWidget.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/MeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
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
	DOREPLIFETIME(UBH_StanceComponent, bWeaponDrawn);
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

	ApplyWeaponDrawnLocal(bWeaponDrawn, false); // initial mirror (sheathed), no transition montage
}

void UBH_StanceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutoSheathTimer);
	}
	Super::EndPlay(EndPlayReason);
}

UBH_StanceMovementProfile* UBH_StanceComponent::GetActiveMovementProfile() const
{
	// A stance without its own relaxed animation set uses the Neutral locomotion while sheathed: Default profile speeds match those clips.
	if (!bWeaponDrawn && !StancesWithRelaxedSet.Contains(CurrentStance))
	{
		return DefaultMovementProfile.Get();
	}
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
	if (NewStance == TAG_Stance_Weapon_Unarmed.GetTag() && bWeaponDrawn)
	{
		SetWeaponDrawn(false); // Unarmed is always sheathed
	}
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

void UBH_StanceComponent::EquipWeaponsFor(FGameplayTag New)
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character || !WeaponLoadouts)
	{
		return;
	}
	TArray<UMeshComponent*> Attached;
	UBH_CombatFunctionLibrary::EquipWeaponsForOverlayPose(Character, WeaponLoadouts, BH_Stance::ToLegacyName(New).ToString(), Attached);
	// The meshes were just respawned in the hand: put them where the drawn state says (no transition montage).
	ApplyWeaponAttachment(bWeaponDrawn);
}

void UBH_StanceComponent::ApplyWeaponAttachment(bool bInHand)
{
	bWeaponsInHand = bInHand;
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		UBH_CombatFunctionLibrary::SetWeaponMeshSheathed(Character, EBH_WeaponSlot::MainHand, !bInHand);
		UBH_CombatFunctionLibrary::SetWeaponMeshSheathed(Character, EBH_WeaponSlot::OffHand, !bInHand);
	}
}

void UBH_StanceComponent::HandleWeaponAttachNotify(bool bToHand)
{
	ApplyWeaponAttachment(bToHand);
}

void UBH_StanceComponent::OnWeaponTransitionMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	// Safety net: an interrupted draw / sheath montage never leaves the weapon mid-air.
	ApplyWeaponAttachment(bWeaponDrawn);
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

// ============================================================================
// Weapon drawn / sheathed
// ============================================================================

bool UBH_StanceComponent::SetWeaponDrawn(bool bDrawn)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return false;
	}
	if (bDrawn && CurrentStance == TAG_Stance_Weapon_Unarmed.GetTag())
	{
		return false; // nothing to draw
	}
	if (bDrawn == bWeaponDrawn)
	{
		RestartAutoSheathTimer(); // activity while already drawn just pushes the sheath time out
		return true;
	}

	bWeaponDrawn = bDrawn;
	ApplyWeaponDrawnLocal(bDrawn, true);
	RestartAutoSheathTimer();
	Owner->ForceNetUpdate();
	return true;
}

bool UBH_StanceComponent::RequestWeaponDrawn(bool bDrawn)
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}
	if (Owner->HasAuthority())
	{
		return SetWeaponDrawn(bDrawn);
	}
	const APawn* Pawn = Cast<APawn>(Owner);
	if (Pawn && Pawn->IsLocallyControlled())
	{
		ServerSetWeaponDrawn(bDrawn);
		return true;
	}
	return false;
}

void UBH_StanceComponent::ToggleWeaponDrawn()
{
	RequestWeaponDrawn(!bWeaponDrawn);
}

void UBH_StanceComponent::NotifyCombatActivity()
{
	SetWeaponDrawn(true);
}

void UBH_StanceComponent::ServerSetWeaponDrawn_Implementation(bool bDrawn)
{
	SetWeaponDrawn(bDrawn);
}

bool UBH_StanceComponent::ServerSetWeaponDrawn_Validate(bool bDrawn)
{
	return true;
}

void UBH_StanceComponent::OnRep_WeaponDrawn()
{
	ApplyWeaponDrawnLocal(bWeaponDrawn, true);
}

void UBH_StanceComponent::ApplyWeaponDrawnLocal(bool bDrawn, bool bPlayMontage)
{
	if (UAbilitySystemComponent* ASC = bMirrorStanceAsLooseTag ? ResolveASC() : nullptr)
	{
		ASC->SetLooseGameplayTagCount(TAG_State_Weapon_Drawn, bDrawn ? 1 : 0);
		ASC->SetLooseGameplayTagCount(TAG_State_Weapon_Sheathed, bDrawn ? 0 : 1);
	}

	bool bMontagePlayed = false;
	if (bPlayMontage)
	{
		const FBH_WeaponStateMontages* Montages = WeaponStateMontages.Find(CurrentStance);
		UAnimMontage* Montage = Montages ? (bDrawn ? Montages->Draw.Get() : Montages->Sheath.Get()) : nullptr;
		const ACharacter* Character = Cast<ACharacter>(GetOwner());
		UAnimInstance* AnimInstance = Character && Character->GetMesh() ? Character->GetMesh()->GetAnimInstance() : nullptr;
		if (Montage && AnimInstance)
		{
			// cosmetic UpperBody montage, never through the ASC; the BH Weapon Attach notify moves the weapon at the grab / release frame
			if (AnimInstance->Montage_Play(Montage, 1.f) > 0.f)
			{
				bMontagePlayed = true;
				FOnMontageEnded EndDelegate = FOnMontageEnded::CreateUObject(this, &UBH_StanceComponent::OnWeaponTransitionMontageEnded);
				AnimInstance->Montage_SetEndDelegate(EndDelegate, Montage);
			}
		}
	}
	if (!bMontagePlayed)
	{
		ApplyWeaponAttachment(bDrawn); // no clip (Unarmed, stance without montages, late joiner, initial mirror): snap
	}

	OnWeaponDrawnChanged.Broadcast(bDrawn);
}

void UBH_StanceComponent::RestartAutoSheathTimer()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	FTimerManager& Timers = World->GetTimerManager();
	Timers.ClearTimer(AutoSheathTimer);
	if (bWeaponDrawn && AutoSheathDelay > 0.f)
	{
		Timers.SetTimer(AutoSheathTimer, this, &UBH_StanceComponent::AutoSheathTick, AutoSheathDelay, false);
	}
}

void UBH_StanceComponent::AutoSheathTick()
{
	if (!bWeaponDrawn)
	{
		return;
	}

	bool bBusy = false;
	if (const APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		if (const AController* Controller = Pawn->GetController())
		{
			const UBH_LockOnComponent* LockOn = Controller->FindComponentByClass<UBH_LockOnComponent>();
			bBusy = LockOn && LockOn->IsLockedOn();
		}
	}
	if (!bBusy)
	{
		if (const UAbilitySystemComponent* ASC = ResolveASC())
		{
			for (const FGameplayTag& Busy : { TAG_State_Combat_Attacking.GetTag(), TAG_State_Combat_Blocking.GetTag(), TAG_State_Combat_Parrying.GetTag(),
				TAG_State_Combat_Dodging.GetTag(), TAG_State_Combat_Staggered.GetTag(), TAG_State_Combat_PostureBroken.GetTag() })
			{
				if (ASC->HasMatchingGameplayTag(Busy))
				{
					bBusy = true;
					break;
				}
			}
		}
	}

	if (bBusy)
	{
		RestartAutoSheathTimer();
	}
	else
	{
		SetWeaponDrawn(false);
	}
}
