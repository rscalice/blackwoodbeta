// Blackwood Hollow - Blight Volume Actor (implementation)

#include "Actors/BP_BlightVolume.h"
#include "Components/BoxComponent.h"
#include "Components/BPC_HeartFragment.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/OverlapResult.h"
#include "CollisionQueryParams.h"

ABP_BlightVolume::ABP_BlightVolume()
{
	PrimaryActorTick.bCanEverTick = false; // damage is timer-driven, not per-frame
	bReplicates = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	OverlapVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("OverlapVolume"));
	OverlapVolume->SetupAttachment(Root);
	OverlapVolume->SetBoxExtent(FVector(500.f, 500.f, 200.f));
	OverlapVolume->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	OverlapVolume->SetGenerateOverlapEvents(true);
}

void ABP_BlightVolume::BeginPlay()
{
	Super::BeginPlay();

	OverlapVolume->OnComponentBeginOverlap.AddDynamic(this, &ABP_BlightVolume::OnVolumeBeginOverlap);
	OverlapVolume->OnComponentEndOverlap.AddDynamic(this, &ABP_BlightVolume::OnVolumeEndOverlap);

	if (HasAuthority())
	{
		GetWorldTimerManager().SetTimer(DamageTickTimerHandle, this, &ABP_BlightVolume::DamageTick, TickInterval, true);
		SubscribeToOverloadEvents();
	}
}

void ABP_BlightVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(DamageTickTimerHandle);
	GetWorldTimerManager().ClearTimer(SuppressionTimerHandle);
	UnsubscribeFromOverloadEvents();

	Super::EndPlay(EndPlayReason);
}

void ABP_BlightVolume::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void ABP_BlightVolume::OnVolumeBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (!OtherActor || OtherActor == this)
	{
		return;
	}

	OverlappingActors.Add(OtherActor);

	if (HasAuthority())
	{
		if (UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(OtherActor))
		{
			ASC->AddLooseGameplayTag(FBH_GameplayTags::Get().State_Combat_BlightShielded);
		}
	}
}

void ABP_BlightVolume::OnVolumeEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex)
{
	if (!OtherActor)
	{
		return;
	}

	OverlappingActors.Remove(OtherActor);

	if (HasAuthority())
	{
		if (UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(OtherActor))
		{
			ASC->RemoveLooseGameplayTag(FBH_GameplayTags::Get().State_Combat_BlightShielded);
		}
	}
}

void ABP_BlightVolume::DamageTick()
{
	if (bSuppressed)
	{
		return;
	}

	// Copy first: ApplyBlightTickToActor can indirectly trigger gameplay
	// events whose handlers may cause an actor to leave OverlappingActors.
	TArray<TWeakObjectPtr<AActor>> ActorsToTick = OverlappingActors.Array();
	for (const TWeakObjectPtr<AActor>& WeakActor : ActorsToTick)
	{
		if (AActor* TargetActor = WeakActor.Get())
		{
			ApplyBlightTickToActor(TargetActor);
		}
	}
}

void ABP_BlightVolume::ApplyBlightTickToActor(AActor* TargetActor)
{
	if (!TargetActor || DamagePerTick <= 0.f)
	{
		return;
	}

	UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(TargetActor);
	const UAH_AttributeSet* AttributeSet = ASC ? ASC->GetSet<UAH_AttributeSet>() : nullptr;

	float RemainingDamage = DamagePerTick;

	// BlightResistance mitigates first, at the attribute-set level.
	if (AttributeSet)
	{
		RemainingDamage *= (1.f - AttributeSet->GetBlightResistance());
	}

	// The Heart-Fragment's Blight shield absorbs next, if the owner has one.
	if (UBPC_HeartFragment* HeartFragment = TargetActor->FindComponentByClass<UBPC_HeartFragment>())
	{
		RemainingDamage = HeartFragment->AbsorbBlightDamage(RemainingDamage);
	}

	if (RemainingDamage <= 0.f)
	{
		return;
	}

	if (ASC)
	{
		FGameplayEventData EventData;
		EventData.EventTag = FBH_GameplayTags::Get().Event_Combat_BlightDamage;
		EventData.Instigator = this;
		EventData.Target = TargetActor;
		EventData.EventMagnitude = RemainingDamage;
		ASC->HandleGameplayEvent(FBH_GameplayTags::Get().Event_Combat_BlightDamage, &EventData);
	}

	// Fallback direct application for actors with an AttributeSet but no
	// GameplayEffect wired to Event.Combat.BlightDamage yet: nudge Health
	// down directly via the ASC's numeric attribute API so the volume is
	// functional out of the box. Once GE_BlightDamageOverTime exists, prefer
	// driving damage entirely off the event above and remove this fallback.
	if (ASC && AttributeSet)
	{
		const float NewHealth = FMath::Clamp(AttributeSet->GetHealth() - RemainingDamage, 0.f, AttributeSet->GetMaxHealth());
		ASC->ApplyModToAttribute(UAH_AttributeSet::GetHealthAttribute(), EGameplayModOp::Override, NewHealth);
	}
}

void ABP_BlightVolume::SubscribeToOverloadEvents()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	TArray<FOverlapResult> Overlaps;
	FCollisionShape Sphere = FCollisionShape::MakeSphere(OverloadResponseRadius);
	World->OverlapMultiByObjectType(Overlaps, GetActorLocation(), FQuat::Identity,
		FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllDynamicObjects), Sphere);

	TSet<UAbilitySystemComponent*> SeenASCs;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Actor = Overlap.GetActor();
		if (!Actor)
		{
			continue;
		}

		UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Actor);
		if (!ASC || SeenASCs.Contains(ASC))
		{
			continue;
		}
		SeenASCs.Add(ASC);

		FGameplayTagContainer Filter;
		Filter.AddTag(FBH_GameplayTags::Get().Event_Combat_OverloadBurst);

		FDelegateHandle Handle = ASC->AddGameplayEventTagContainerDelegate(
			Filter,
			FGameplayEventTagMulticastDelegate::FDelegate::CreateLambda(
				[this](FGameplayTag EventTag, const FGameplayEventData* Payload)
				{
					HandleOverloadBurstNearby(Payload ? const_cast<AActor*>(Payload->Instigator.Get()) : nullptr);
				}));

		OverloadEventSubscriptions.Add(TPair<TWeakObjectPtr<UAbilitySystemComponent>, FDelegateHandle>(ASC, Handle));
	}
}

void ABP_BlightVolume::UnsubscribeFromOverloadEvents()
{
	FGameplayTagContainer Filter;
	Filter.AddTag(FBH_GameplayTags::Get().Event_Combat_OverloadBurst);

	for (const TPair<TWeakObjectPtr<UAbilitySystemComponent>, FDelegateHandle>& Sub : OverloadEventSubscriptions)
	{
		if (UAbilitySystemComponent* ASC = Sub.Key.Get())
		{
			ASC->RemoveGameplayEventTagContainerDelegate(Filter, Sub.Value);
		}
	}
	OverloadEventSubscriptions.Reset();
}

void ABP_BlightVolume::HandleOverloadBurstNearby(AActor* BurstInstigator)
{
	if (!HasAuthority())
	{
		return;
	}

	bSuppressed = true;
	GetWorldTimerManager().SetTimer(SuppressionTimerHandle, this, &ABP_BlightVolume::OnSuppressionExpired, OverloadSuppressionDuration, false);

	// Also immediately relieve anyone already standing in the fog.
	for (const TWeakObjectPtr<AActor>& WeakActor : OverlappingActors)
	{
		if (AActor* TargetActor = WeakActor.Get())
		{
			if (UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(TargetActor))
			{
				ASC->RemoveLooseGameplayTag(FBH_GameplayTags::Get().State_Combat_BlightShielded);
			}
		}
	}
}

void ABP_BlightVolume::OnSuppressionExpired()
{
	bSuppressed = false;

	for (const TWeakObjectPtr<AActor>& WeakActor : OverlappingActors)
	{
		if (AActor* TargetActor = WeakActor.Get())
		{
			if (UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(TargetActor))
			{
				ASC->AddLooseGameplayTag(FBH_GameplayTags::Get().State_Combat_BlightShielded);
			}
		}
	}
}
