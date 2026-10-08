// Blackwood Hollow - consumable helpers (implementation)

#include "Consumables/BH_ConsumableLibrary.h"
#include "Consumables/BH_ConsumableEffects.h"
#include "Consumables/BH_WardenSanctuary.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "Interaction/BH_InteractorComponent.h"
#include "Loot/BH_LootLibrary.h"
#include "Loot/BH_LootTypes.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Animation/AnimMontage.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Modules/ModuleManager.h"
#include "NarrativeItem.h"

DEFINE_LOG_CATEGORY_STATIC(LogBHConsumable, Log, All);

FBH_ConsumableDefinition UBH_ConsumableLibrary::MakeSapDefinition()
{
	FBH_ConsumableDefinition Definition;
	Definition.ItemClass = TSoftClassPtr<UNarrativeItem>(FSoftObjectPath(BH_LootPaths::HeartwoodSap));
	Definition.Montage = TSoftObjectPtr<UAnimMontage>(FSoftObjectPath(BH_ConsumableDefaults::DrinkMontagePath));
	Definition.UseDuration = BH_ConsumableDefaults::SapUseSeconds;
	Definition.EffectKind = EBH_ConsumableEffectKind::HealOverTime;
	Definition.HealFractionOfMaxHealth = BH_ConsumableDefaults::SapHealFraction;
	Definition.HealDuration = BH_ConsumableDefaults::SapHealSeconds;
	return Definition;
}

FBH_ConsumableDefinition UBH_ConsumableLibrary::MakeIncenseDefinition()
{
	FBH_ConsumableDefinition Definition;
	Definition.ItemClass = TSoftClassPtr<UNarrativeItem>(FSoftObjectPath(BH_LootPaths::WardensIncense));
	Definition.Montage = TSoftObjectPtr<UAnimMontage>(FSoftObjectPath(BH_ConsumableDefaults::CenserMontagePath));
	Definition.UseDuration = BH_ConsumableDefaults::IncenseUseSeconds;
	Definition.EffectKind = EBH_ConsumableEffectKind::DeploySanctuary;
	Definition.SanctuaryRadius = BH_ConsumableDefaults::IncenseRadius;
	Definition.SanctuaryDuration = BH_ConsumableDefaults::IncenseSeconds;
	return Definition;
}

bool UBH_ConsumableLibrary::FindDefinition(TSubclassOf<UNarrativeItem> ItemClass, FBH_ConsumableDefinition& OutDefinition)
{
	if (!ItemClass)
	{
		return false;
	}

	// 1) A DA_Consumable_* data asset for this item wins. Few assets, looked up once per use: a registry scan is cheap enough.
	FAssetRegistryModule& RegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> Assets;
	RegistryModule.Get().GetAssetsByClass(UBH_ConsumableData::StaticClass()->GetClassPathName(), Assets, /*bSearchSubClasses*/ true);
	for (const FAssetData& Asset : Assets)
	{
		const UBH_ConsumableData* Data = Cast<UBH_ConsumableData>(Asset.GetAsset());
		if (Data && Data->Definition.ItemClass.LoadSynchronous() == ItemClass.Get())
		{
			OutDefinition = Data->Definition;
			return true;
		}
	}

	// 2) Built-in defaults for the two Phase 11C consumables, matched by class path.
	const FString ItemPath = ItemClass->GetPathName();
	if (ItemPath.Equals(BH_LootPaths::HeartwoodSap, ESearchCase::IgnoreCase))
	{
		OutDefinition = MakeSapDefinition();
		return true;
	}
	if (ItemPath.Equals(BH_LootPaths::WardensIncense, ESearchCase::IgnoreCase))
	{
		OutDefinition = MakeIncenseDefinition();
		return true;
	}
	return false;
}

bool UBH_ConsumableLibrary::RequestUseConsumable(APawn* Pawn, TSubclassOf<UNarrativeItem> ItemClass)
{
	if (!Pawn || !ItemClass)
	{
		return false;
	}
	// Local pre-check (the inventory replicates to its owner): no item, no request.
	if (UBH_LootLibrary::GetItemCount(Pawn->GetPlayerState(), ItemClass) < 1)
	{
		UE_LOG(LogBHConsumable, Log, TEXT("RequestUseConsumable: %s has none of %s."), *GetNameSafe(Pawn), *GetNameSafe(ItemClass));
		return false;
	}

	if (Pawn->HasAuthority())
	{
		return ActivateOnServer(Pawn, ItemClass);
	}
	if (!Pawn->IsLocallyControlled())
	{
		return false;
	}
	UBH_InteractorComponent* Interactor = UBH_InteractorComponent::Find(Pawn);
	if (!Interactor)
	{
		return false;
	}
	Interactor->ServerUseConsumable(ItemClass);
	return true;
}

bool UBH_ConsumableLibrary::ActivateOnServer(APawn* Pawn, TSubclassOf<UNarrativeItem> ItemClass)
{
	if (!Pawn || !Pawn->HasAuthority() || !ItemClass)
	{
		return false;
	}
	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Pawn);
	if (!ASC)
	{
		return false;
	}

	FGameplayEventData Payload;
	Payload.EventTag = TAG_Event_Consumable_Use.GetTag();
	Payload.Instigator = Pawn;
	Payload.Target = Pawn;
	Payload.OptionalObject = ItemClass.Get();
	return ASC->HandleGameplayEvent(TAG_Event_Consumable_Use.GetTag(), &Payload) > 0;
}

bool UBH_ConsumableLibrary::ApplyConsumableEffect(AActor* Avatar, const FBH_ConsumableDefinition& Definition)
{
	UWorld* EffectWorld = Avatar ? Avatar->GetWorld() : nullptr;
	if (!Avatar || !Avatar->HasAuthority() || !EffectWorld)
	{
		return false;
	}

	switch (Definition.EffectKind)
	{
	case EBH_ConsumableEffectKind::HealOverTime:
	{
		UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Avatar);
		return UBH_GE_HeartwoodSapHeal::ApplyHeal(ASC, Definition.HealFractionOfMaxHealth, Definition.HealDuration).IsValid();
	}

	case EBH_ConsumableEffectKind::DeploySanctuary:
	{
		// In front of the user, on the ground (feet height first, then snapped down by a short trace).
		float FeetOffset = 0.f;
		if (const ACharacter* AvatarCharacter = Cast<ACharacter>(Avatar))
		{
			if (const UCapsuleComponent* Capsule = AvatarCharacter->GetCapsuleComponent())
			{
				FeetOffset = Capsule->GetScaledCapsuleHalfHeight();
			}
		}
		FVector Spot = Avatar->GetActorLocation() + Avatar->GetActorForwardVector() * Definition.SpawnForwardOffset - FVector(0.f, 0.f, FeetOffset);

		FHitResult GroundHit;
		FCollisionQueryParams GroundParams(SCENE_QUERY_STAT(ConsumableSanctuaryGround), /*bTraceComplex*/ false, Avatar);
		if (EffectWorld->LineTraceSingleByChannel(GroundHit, Spot + FVector(0.f, 0.f, 120.f), Spot - FVector(0.f, 0.f, 300.f), ECC_Visibility, GroundParams))
		{
			Spot = GroundHit.ImpactPoint;
		}

		TSubclassOf<ABH_WardenSanctuary> SanctuaryClass = Definition.SanctuaryClass;
		if (!SanctuaryClass)
		{
			SanctuaryClass = ABH_WardenSanctuary::StaticClass();
		}

		const FTransform SpawnTransform(FRotator(0.f, Avatar->GetActorRotation().Yaw, 0.f), Spot);
		ABH_WardenSanctuary* Sanctuary = EffectWorld->SpawnActorDeferred<ABH_WardenSanctuary>(SanctuaryClass, SpawnTransform, Avatar,
			Cast<APawn>(Avatar), ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Sanctuary)
		{
			return false;
		}
		Sanctuary->Radius = Definition.SanctuaryRadius;
		Sanctuary->Duration = Definition.SanctuaryDuration;
		Sanctuary->FinishSpawning(SpawnTransform);
		return true;
	}
	}
	return false;
}
