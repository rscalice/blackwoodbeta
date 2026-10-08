// Blackwood Hollow - Overload Burst gameplay ability base (implementation)

#include "AbilitySystem/Abilities/AH_GA_OverloadBurst.h"
#include "AbilitySystem/BH_CombatFunctionLibrary.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "Actors/BP_BlightVolume.h"
#include "Combat/BH_CombatTeam.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"

UAH_GA_OverloadBurst::UAH_GA_OverloadBurst()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;

	// Heart-Fragment loadout data (no cost, 45 s cooldown via UAH_GA_FragmentBase; a Blueprint child can override CooldownDuration).
	CooldownDuration = 45.f;
	CooldownTags.AddTag(TAG_Cooldown_Fragment_OverloadBurst);
	FragmentName = NSLOCTEXT("BlackwoodHollow", "Fragment_OverloadBurst", "Overload Burst");
	SlotIndexHint = 0;
	FragmentCategory = TAG_Fragment_Category_Offensive;
}

void UAH_GA_OverloadBurst::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;

	// Phase 10B: no protective buff (the old shield recharge is gone) and the Blight meter is left alone.
	// The loose tag only marks the casting window for animation / UI.
	if (ASC)
	{
		ASC->AddLooseGameplayTag(FBH_GameplayTags::Get().State_Combat_Overloading);
		bHoldingOverloadingTag = true;
	}

	if (Avatar && Avatar->HasAuthority())
	{
		const FVector Center = Avatar->GetActorLocation();
		ClearNearbyFog(Avatar, Center);
		StaggerNearbyEnemies(Avatar, Center);

		// Informational broadcast (BP_BlightVolume no longer listens; it is cleared directly above).
		if (ASC)
		{
			FGameplayEventData EventData;
			EventData.EventTag = FBH_GameplayTags::Get().Event_Combat_OverloadBurst;
			EventData.Instigator = Avatar;
			EventData.Target = Avatar;
			EventData.EventMagnitude = FogClearRadius;
			ASC->HandleGameplayEvent(EventData.EventTag, &EventData);
		}
	}

	K2_OnOverloadBurstActivated();

	if (Avatar)
	{
		Avatar->GetWorldTimerManager().SetTimer(BurstDurationTimerHandle, FTimerDelegate::CreateWeakLambda(this,
			[this, Handle, ActorInfo, ActivationInfo]()
			{
				EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
			}), BurstDuration, false);
	}
	else
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
	}
}

void UAH_GA_OverloadBurst::ClearNearbyFog(const AActor* Avatar, const FVector& Center) const
{
	UWorld* World = Avatar ? Avatar->GetWorld() : nullptr;
	if (!World || FogClearRadius <= 0.f)
	{
		return;
	}

	for (TActorIterator<ABP_BlightVolume> It(World); It; ++It)
	{
		ABP_BlightVolume* Volume = *It;
		if (Volume && Volume->IntersectsSphere(Center, FogClearRadius))
		{
			Volume->ClearFog();
		}
	}
}

void UAH_GA_OverloadBurst::StaggerNearbyEnemies(AActor* Avatar, const FVector& Center) const
{
	UWorld* World = Avatar ? Avatar->GetWorld() : nullptr;
	if (!World || BurstRadius <= 0.f)
	{
		return;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(BH_OverloadBurst), false, Avatar);
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByObjectType(Overlaps, Center, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn),
		FCollisionShape::MakeSphere(BurstRadius), Params);

	TSet<AActor*> Handled;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Enemy = Overlap.GetActor();
		if (!Enemy || Enemy == Avatar || Handled.Contains(Enemy))
		{
			continue;
		}
		Handled.Add(Enemy);

		// Corrupted = the enemy team (players, allies and neutral props are untouched).
		if (UBH_CombatFunctionLibrary::GetCombatTeam(Enemy) != EBH_CombatTeam::Enemies)
		{
			continue;
		}
		UAbilitySystemComponent* EnemyASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Enemy);
		if (!EnemyASC || EnemyASC->HasMatchingGameplayTag(TAG_State_Combat_Dead))
		{
			continue;
		}

		// The existing stagger path: UAH_GA_HitReaction triggers on Event.Combat.DamageReceived (hyper armor / posture break / death
		// still suppress it). The event carries no damage; nothing is applied to Health.
		FGameplayEventData StaggerEvent;
		StaggerEvent.EventTag = TAG_Event_Combat_DamageReceived;
		StaggerEvent.Instigator = Avatar;
		StaggerEvent.Target = Enemy;
		StaggerEvent.EventMagnitude = EnemyStaggerMagnitude;
		EnemyASC->HandleGameplayEvent(TAG_Event_Combat_DamageReceived, &StaggerEvent);
	}
}

void UAH_GA_OverloadBurst::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (ActorInfo && ActorInfo->AvatarActor.IsValid())
	{
		ActorInfo->AvatarActor->GetWorldTimerManager().ClearTimer(BurstDurationTimerHandle);

		if (UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get())
		{
			if (bHoldingOverloadingTag)
			{
				ASC->RemoveLooseGameplayTag(FBH_GameplayTags::Get().State_Combat_Overloading);
			}
		}
		bHoldingOverloadingTag = false;
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UAH_GA_OverloadBurst::FinishBurst()
{
	// Reserved for native cleanup beyond EndAbility, if this class grows.
}
