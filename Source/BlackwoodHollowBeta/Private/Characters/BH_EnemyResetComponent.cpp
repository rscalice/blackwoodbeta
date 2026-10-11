// Blackwood Hollow - enemy reset after a party wipe (implementation)

#include "Characters/BH_EnemyResetComponent.h"
#include "AI/BH_AIController.h"
#include "AI/BH_CrabAIController.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystem/StatusEffects/BH_StatusEffect.h"
#include "Characters/BH_EnemyBase.h"
#include "Combat/BH_CombatIdentityComponent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogBHEnemyReset, Log, All);

namespace BH_EnemyReset_Private
{
	/** Every live reset component (server). Filtered by world in ResetAllInWorld, so several PIE worlds do not mix. */
	static TArray<TWeakObjectPtr<UBH_EnemyResetComponent>> GInstances;

	static void SetAttributeToMax(UAbilitySystemComponent& ASC, const FGameplayAttribute& Current, const FGameplayAttribute& Max)
	{
		if (ASC.HasAttributeSetForAttribute(Current) && ASC.HasAttributeSetForAttribute(Max))
		{
			ASC.SetNumericAttributeBase(Current, ASC.GetNumericAttribute(Max));
		}
	}
}

UBH_EnemyResetComponent::UBH_EnemyResetComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false; // only ticks while walking home
	SetIsReplicatedByDefault(false);
}

UBH_EnemyResetComponent* UBH_EnemyResetComponent::Find(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UBH_EnemyResetComponent>() : nullptr;
}

UBH_EnemyResetComponent* UBH_EnemyResetComponent::EnsureOn(AActor* Actor)
{
	if (!Actor || !Actor->HasAuthority())
	{
		return nullptr;
	}
	if (UBH_EnemyResetComponent* Existing = Find(Actor))
	{
		Existing->CaptureHome();
		return Existing;
	}
	UBH_EnemyResetComponent* Created = NewObject<UBH_EnemyResetComponent>(Actor, TEXT("EnemyResetComponent"));
	if (!Created)
	{
		return nullptr;
	}
	Created->CaptureHome();
	Created->RegisterComponent();
	return Created;
}

int32 UBH_EnemyResetComponent::ResetAllInWorld(const UWorld* ForWorld)
{
	if (!ForWorld)
	{
		return 0;
	}
	int32 Count = 0;
	const TArray<TWeakObjectPtr<UBH_EnemyResetComponent>> Snapshot = BH_EnemyReset_Private::GInstances;
	for (const TWeakObjectPtr<UBH_EnemyResetComponent>& Weak : Snapshot)
	{
		UBH_EnemyResetComponent* Comp = Weak.Get();
		if (!Comp || Comp->GetWorld() != ForWorld || !Comp->bResetOnPartyWipe || Comp->IsOwnerDead())
		{
			continue;
		}
		Comp->ResetNow();
		++Count;
	}
	UE_LOG(LogBHEnemyReset, Log, TEXT("Party wipe: %d enemies reset."), Count);
	return Count;
}

void UBH_EnemyResetComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		CaptureHome();
		BH_EnemyReset_Private::GInstances.AddUnique(this);
	}
}

void UBH_EnemyResetComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	BH_EnemyReset_Private::GInstances.Remove(this);
	BH_EnemyReset_Private::GInstances.RemoveAll([](const TWeakObjectPtr<UBH_EnemyResetComponent>& Weak) { return !Weak.IsValid(); });
	if (bReturning)
	{
		SetControllerResetHold(false);
		bReturning = false;
	}
	Super::EndPlay(EndPlayReason);
}

void UBH_EnemyResetComponent::CaptureHome()
{
	if (!bHomeCaptured && GetOwner())
	{
		HomeTransform = FTransform(GetOwner()->GetActorRotation(), GetOwner()->GetActorLocation());
		bHomeCaptured = true;
	}
}

void UBH_EnemyResetComponent::SetHomeTransform(const FTransform& NewHome)
{
	HomeTransform = FTransform(NewHome.Rotator(), NewHome.GetLocation());
	bHomeCaptured = true;
}

bool UBH_EnemyResetComponent::IsOwnerDead() const
{
	const AActor* Actor = GetOwner();
	if (!Actor)
	{
		return true;
	}
	if (const ABH_EnemyBase* EnemyBase = Cast<ABH_EnemyBase>(Actor))
	{
		if (EnemyBase->IsEnemyDead())
		{
			return true;
		}
	}
	if (const UBH_CombatIdentityComponent* Identity = UBH_CombatIdentityComponent::Find(Actor))
	{
		if (Identity->IsDead())
		{
			return true;
		}
	}
	const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor);
	return ASC && ASC->HasMatchingGameplayTag(TAG_State_Combat_Dead);
}

bool UBH_EnemyResetComponent::IsSeenByAnyPlayer() const
{
	const AActor* Actor = GetOwner();
	const UWorld* ComponentWorld = GetWorld();
	if (!Actor || !ComponentWorld)
	{
		return false;
	}
	const FVector EnemyLocation = Actor->GetActorLocation();
	const double MaxDistSq = FMath::Square(static_cast<double>(SightCheckDistance));
	for (FConstPlayerControllerIterator It = ComponentWorld->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		if (!PC)
		{
			continue;
		}
		FVector ViewLocation = FVector::ZeroVector;
		FRotator ViewRotation = FRotator::ZeroRotator;
		PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
		const FVector ToEnemy = EnemyLocation - ViewLocation;
		if (ToEnemy.SizeSquared() > MaxDistSq)
		{
			continue;
		}
		if (FVector::DotProduct(ToEnemy.GetSafeNormal(), ViewRotation.Vector()) < SightMinDot)
		{
			continue;
		}
		if (PC->LineOfSightTo(Actor, ViewLocation))
		{
			return true;
		}
	}
	return false;
}

void UBH_EnemyResetComponent::SetControllerResetHold(bool bHold)
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	AController* Controller = Pawn ? Pawn->GetController() : nullptr;
	if (ABH_AIController* Brain = Cast<ABH_AIController>(Controller))
	{
		Brain->SetResetHold(bHold);
	}
	else if (ABH_CrabAIController* CrabBrain = Cast<ABH_CrabAIController>(Controller))
	{
		CrabBrain->SetResetHold(bHold);
	}
}

void UBH_EnemyResetComponent::DestroyRegistered(TArray<TWeakObjectPtr<AActor>>& Registered)
{
	const TArray<TWeakObjectPtr<AActor>> Snapshot = Registered;
	Registered.Reset();
	for (const TWeakObjectPtr<AActor>& Weak : Snapshot)
	{
		if (AActor* Registered_Actor = Weak.Get())
		{
			if (IsValid(Registered_Actor))
			{
				Registered_Actor->Destroy();
			}
		}
	}
}

void UBH_EnemyResetComponent::RegisterAdd(AActor* Add)
{
	if (Add && GetOwner() && GetOwner()->HasAuthority())
	{
		RegisteredAdds.AddUnique(Add);
	}
}

void UBH_EnemyResetComponent::RegisterHazard(AActor* Hazard)
{
	if (Hazard && GetOwner() && GetOwner()->HasAuthority())
	{
		RegisteredHazards.AddUnique(Hazard);
	}
}

void UBH_EnemyResetComponent::TeleportHome()
{
	AActor* Actor = GetOwner();
	if (!Actor)
	{
		return;
	}
	Actor->TeleportTo(HomeTransform.GetLocation(), HomeTransform.Rotator(), /*bIsATest*/ false, /*bNoCheck*/ true);
	if (const ACharacter* Character = Cast<ACharacter>(Actor))
	{
		if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
		}
	}
}

void UBH_EnemyResetComponent::FinishReturn()
{
	bReturning = false;
	ReturnElapsed = 0.f;
	SetComponentTickEnabled(false);
	if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
		}
	}
	SetControllerResetHold(false); // the brain scans for targets again
}

void UBH_EnemyResetComponent::ResetNow()
{
	AActor* Actor = GetOwner();
	if (!Actor || !Actor->HasAuthority() || !bResetOnPartyWipe || IsOwnerDead())
	{
		return;
	}
	CaptureHome();

	// 1) Stats, abilities, statuses.
	if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor))
	{
		ASC->CancelAllAbilities();

		TArray<FBH_ActiveStatusEffect> Statuses;
		UBH_StatusEffectLibrary::GetActiveStatusEffects(ASC, Statuses);
		for (const FBH_ActiveStatusEffect& Status : Statuses)
		{
			if (Status.Handle.IsValid())
			{
				ASC->RemoveActiveGameplayEffect(Status.Handle);
			}
		}

		if (ASC->HasMatchingGameplayTag(TAG_State_Combat_PostureBroken))
		{
			FGameplayTagContainer BrokenTags;
			BrokenTags.AddTag(TAG_State_Combat_PostureBroken);
			ASC->RemoveActiveEffectsWithGrantedTags(BrokenTags);
			ASC->SetLooseGameplayTagCount(TAG_State_Combat_PostureBroken, 0, EGameplayTagReplicationState::TagOnly);
		}

		BH_EnemyReset_Private::SetAttributeToMax(*ASC, UAH_AttributeSet::GetHealthAttribute(), UAH_AttributeSet::GetMaxHealthAttribute());
		BH_EnemyReset_Private::SetAttributeToMax(*ASC, UAH_AttributeSet::GetPostureAttribute(), UAH_AttributeSet::GetMaxPostureAttribute());
	}

	// 2) Aggro, attack token, AI state. The hold keeps the brain from re-acquiring a target while it walks home.
	SetControllerResetHold(true);
	if (UBH_CombatIdentityComponent* Identity = UBH_CombatIdentityComponent::Find(Actor))
	{
		Identity->SetAggroTarget(nullptr);
	}
	if (ABH_EnemyBase* EnemyBase = Cast<ABH_EnemyBase>(Actor))
	{
		EnemyBase->SetAggroTarget(nullptr);
	}

	// 3) Boss / elite hook.
	PhaseIndex = 0;
	DestroyRegistered(RegisteredAdds);
	DestroyRegistered(RegisteredHazards);

	// 4) Back to the spawn point: unseen = teleport, seen = walk (re-checked while walking).
	if (const ACharacter* Character = Cast<ACharacter>(Actor))
	{
		if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
		}
	}
	FVector ToHome = HomeTransform.GetLocation() - Actor->GetActorLocation();
	ToHome.Z = 0.0;
	const bool bAtHome = ToHome.Size() <= static_cast<double>(ArriveRadius);
	if (bAtHome || !IsSeenByAnyPlayer())
	{
		TeleportHome();
		FinishReturn();
	}
	else
	{
		bReturning = true;
		ReturnElapsed = 0.f;
		SightRecheckLeft = SightRecheckInterval;
		SetComponentTickEnabled(true);
	}

	OnWipeReset.Broadcast(this);
	UE_LOG(LogBHEnemyReset, Verbose, TEXT("%s reset (%s)."), *GetNameSafe(Actor), bReturning ? TEXT("walking home") : TEXT("at home"));
}

void UBH_EnemyResetComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bReturning)
	{
		SetComponentTickEnabled(false);
		return;
	}
	AActor* Actor = GetOwner();
	if (!Actor || IsOwnerDead())
	{
		FinishReturn();
		return;
	}

	ReturnElapsed += DeltaTime;
	FVector ToHome = HomeTransform.GetLocation() - Actor->GetActorLocation();
	ToHome.Z = 0.0;
	if (ToHome.Size() <= static_cast<double>(ArriveRadius))
	{
		FinishReturn();
		return;
	}
	if (ReturnElapsed >= MaxWalkSeconds)
	{
		TeleportHome();
		FinishReturn();
		return;
	}

	SightRecheckLeft -= DeltaTime;
	if (SightRecheckLeft <= 0.f)
	{
		SightRecheckLeft = SightRecheckInterval;
		if (!IsSeenByAnyPlayer())
		{
			TeleportHome();
			FinishReturn();
			return;
		}
	}

	if (APawn* Pawn = Cast<APawn>(Actor))
	{
		const FVector Direction = ToHome.GetSafeNormal();
		Pawn->AddMovementInput(Direction, 1.f);
		if (AController* Controller = Pawn->GetController())
		{
			Controller->SetControlRotation(FRotator(0.f, Direction.Rotation().Yaw, 0.f));
		}
	}
}
