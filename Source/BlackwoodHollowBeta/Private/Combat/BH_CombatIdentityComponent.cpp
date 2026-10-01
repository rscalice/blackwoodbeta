// Blackwood Hollow - combat identity for non-ABH_EnemyBase characters (implementation)

#include "Combat/BH_CombatIdentityComponent.h"
#include "Combat/BH_StanceWatcherComponent.h"
#include "Combat/BH_WeaponLoadoutDataAsset.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystem/BH_CombatFunctionLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "Abilities/GameplayAbility.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Materials/MaterialInterface.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "TimerManager.h"

UBH_CombatIdentityComponent::UBH_CombatIdentityComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	DisplayName = NSLOCTEXT("BlackwoodHollow", "IdentityDefaultName", "Enemy");
}

void UBH_CombatIdentityComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UBH_CombatIdentityComponent, DisplayName);
	DOREPLIFETIME(UBH_CombatIdentityComponent, CombatTeam);
}

UBH_CombatIdentityComponent* UBH_CombatIdentityComponent::Find(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UBH_CombatIdentityComponent>() : nullptr;
}

UAbilitySystemComponent* UBH_CombatIdentityComponent::ResolveASC() const
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}
	if (const IAbilitySystemInterface* Interface = Cast<IAbilitySystemInterface>(Owner))
	{
		if (UAbilitySystemComponent* ASC = Interface->GetAbilitySystemComponent())
		{
			return ASC;
		}
	}
	return Owner->FindComponentByClass<UAbilitySystemComponent>();
}

void UBH_CombatIdentityComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	// The stance watcher resolves loadouts from an owner variable or its own fallback: feed it ours (every machine).
	if (UBH_StanceWatcherComponent* Watcher = UBH_StanceWatcherComponent::FindStanceWatcher(Owner))
	{
		if (WeaponLoadouts && !Watcher->FallbackLoadouts)
		{
			Watcher->FallbackLoadouts = WeaponLoadouts;
		}
	}

	ApplyCosmetics();
	ApplyCollision();
	// GASP re-initialises its meshes shortly after possession; re-apply once so late changes are covered.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(CosmeticsTimer, this, &UBH_CombatIdentityComponent::ApplyLateSetup, 0.6f, false);
	}

	if (Owner->HasAuthority())
	{
		InitServer();
	}
}

void UBH_CombatIdentityComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(StanceTimer);
		World->GetTimerManager().ClearTimer(ResetTimer);
		World->GetTimerManager().ClearTimer(CosmeticsTimer);
	}
	if (HealthZeroHandle.IsValid())
	{
		if (UAbilitySystemComponent* ASC = ResolveASC())
		{
			if (UAH_AttributeSet* Set = const_cast<UAH_AttributeSet*>(ASC->GetSet<UAH_AttributeSet>()))
			{
				Set->OnHealthZero.Remove(HealthZeroHandle);
			}
		}
		HealthZeroHandle.Reset();
	}
	Super::EndPlay(EndPlayReason);
}

void UBH_CombatIdentityComponent::ApplyLateSetup()
{
	ApplyCosmetics();
	ApplyCollision();
}

void UBH_CombatIdentityComponent::ApplyCollision()
{
	// Explicit both ways: SetupCombatCharacter (server) also blocks pawns, so bBlockPawns = false must undo that.
	if (AActor* Owner = GetOwner())
	{
		if (UCapsuleComponent* Capsule = Owner->FindComponentByClass<UCapsuleComponent>())
		{
			Capsule->SetCollisionResponseToChannel(ECC_Pawn, bBlockPawns ? ECR_Block : ECR_Ignore);
		}
	}
}

void UBH_CombatIdentityComponent::ApplyInitialStats(UAbilitySystemComponent* ASC) const
{
	if (!ASC || !ASC->GetSet<UAH_AttributeSet>())
	{
		return;
	}
	// Max values first, then the current value follows its max.
	if (bOverrideMaxHealth)
	{
		ASC->SetNumericAttributeBase(UAH_AttributeSet::GetMaxHealthAttribute(), MaxHealth);
		ASC->SetNumericAttributeBase(UAH_AttributeSet::GetHealthAttribute(), MaxHealth);
	}
	if (bOverrideMaxPosture)
	{
		ASC->SetNumericAttributeBase(UAH_AttributeSet::GetMaxPostureAttribute(), MaxPosture);
		ASC->SetNumericAttributeBase(UAH_AttributeSet::GetPostureAttribute(), MaxPosture);
	}
	if (bOverrideMaxStamina)
	{
		ASC->SetNumericAttributeBase(UAH_AttributeSet::GetMaxStaminaAttribute(), MaxStamina);
		ASC->SetNumericAttributeBase(UAH_AttributeSet::GetStaminaAttribute(), MaxStamina);
	}
	if (bOverrideAttackPower)
	{
		ASC->SetNumericAttributeBase(UAH_AttributeSet::GetAttackPowerAttribute(), AttackPower);
	}
	if (bOverrideDefense)
	{
		ASC->SetNumericAttributeBase(UAH_AttributeSet::GetDefenseAttribute(), Defense);
	}
	if (bOverrideAttackSpeed)
	{
		ASC->SetNumericAttributeBase(UAH_AttributeSet::GetAttackSpeedAttribute(), AttackSpeed);
	}
}

void UBH_CombatIdentityComponent::ApplyCosmetics()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	TArray<USkeletalMeshComponent*> Meshes;
	Owner->GetComponents<USkeletalMeshComponent>(Meshes);
	for (USkeletalMeshComponent* Mesh : Meshes)
	{
		if (!Mesh || Mesh->ComponentHasTag(FName(TEXT("BH.Weapon"))))
		{
			continue;
		}
		// Anim notifies (hitboxes, combo windows) must fire even when the mesh is not rendered.
		Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		if (OverlayMaterial)
		{
			Mesh->SetOverlayMaterial(OverlayMaterial);
		}
	}
}

void UBH_CombatIdentityComponent::InitServer()
{
	AActor* Owner = GetOwner();
	UAbilitySystemComponent* ASC = ResolveASC();
	if (!Owner || !ASC)
	{
		UE_LOG(LogBHCombat, Warning, TEXT("CombatIdentity: no AbilitySystemComponent on '%s'."), *GetNameSafe(Owner));
		return;
	}

	// AI-controlled: GEs only need to replicate minimally; tags / attributes still replicate.
	ASC->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);

	// A null OverloadBurst class is safe (SetupCombatCharacter only grants it when set).
	UBH_CombatFunctionLibrary::SetupCombatCharacter(Owner, nullptr);
	ApplyInitialStats(ASC);
	ApplyCollision();
	UBH_CombatFunctionLibrary::GrantCombatAbilities(Owner, DefaultAbilities);

	if (UAH_AttributeSet* Set = const_cast<UAH_AttributeSet*>(ASC->GetSet<UAH_AttributeSet>()))
	{
		HealthZeroHandle = Set->OnHealthZero.AddUObject(this, &UBH_CombatIdentityComponent::HandleHealthZero);
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(StanceTimer, this, &UBH_CombatIdentityComponent::ApplyStartingStance, FMath::Max(0.01f, StartingStanceDelay), false);
	}
}

void UBH_CombatIdentityComponent::ApplyStartingStance()
{
	if (AActor* Owner = GetOwner())
	{
		if (!StartingStance.IsNone())
		{
			UBH_CombatFunctionLibrary::ApplyOverlayPoseByDisplayName(Owner, StartingStance.ToString());
		}
	}
}

void UBH_CombatIdentityComponent::HandleHealthZero(AActor* Killer)
{
	// The attribute set adds the loose State.Combat.Dead tag right after this broadcast.
	bDead = true;

	if (UAbilitySystemComponent* ASC = ResolveASC())
	{
		ASC->CancelAllAbilities();
	}

	if (UWorld* World = GetWorld())
	{
		if (bResetOnDeath)
		{
			World->GetTimerManager().SetTimer(ResetTimer, this, &UBH_CombatIdentityComponent::ResetAfterDeath, FMath::Max(0.1f, ResetDelay), false);
		}
	}

	OnDeath.Broadcast(Killer);
}

void UBH_CombatIdentityComponent::ResetAfterDeath()
{
	UAbilitySystemComponent* ASC = ResolveASC();
	const UAH_AttributeSet* Set = ASC ? ASC->GetSet<UAH_AttributeSet>() : nullptr;
	if (!ASC || !Set)
	{
		return;
	}

	ApplyInitialStats(ASC); // archetype values (not the constructor defaults)
	ASC->SetNumericAttributeBase(UAH_AttributeSet::GetHealthAttribute(), Set->GetMaxHealth());
	ASC->SetNumericAttributeBase(UAH_AttributeSet::GetPostureAttribute(), Set->GetMaxPosture());
	ASC->SetNumericAttributeBase(UAH_AttributeSet::GetStaminaAttribute(), Set->GetMaxStamina());
	ASC->SetLooseGameplayTagCount(TAG_State_Combat_Dead, 0);
	ASC->SetLooseGameplayTagCount(TAG_State_Combat_PostureBroken, 0);
	bDead = false;

	OnReset.Broadcast();
}
