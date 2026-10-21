// Blackwood Hollow - server-side hint trigger watcher (Phase 13E, implementation)

#include "Hints/BH_PlayerHintComponent.h"
#include "Hints/BH_HintData.h"
#include "Hints/BH_HintLibrary.h"
#include "Hints/BH_HintSubsystem.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Characters/BH_EnemyBase.h"
#include "Combat/BH_CombatIdentityComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Player/BH_PlayerDeathComponent.h"
#include "TimerManager.h"

namespace
{
	/** The pawn must have been alive this long before attribute and tag triggers run, so spawn-time attribute initialisation cannot fire a hint. */
	constexpr float PawnSettleSeconds = 1.5f;
}

UBH_PlayerHintComponent::UBH_PlayerHintComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UBH_PlayerHintComponent::BeginPlay()
{
	Super::BeginPlay();

	const UBH_HintSettings* Settings = UBH_HintSettings::Get();
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Settings->bHintsEnabled)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(PollTimer, this, &UBH_PlayerHintComponent::Poll, Settings->ServerPollInterval, true);
	}
	IdentityAggroHandle = UBH_CombatIdentityComponent::OnAnyAggroTargetChanged().AddUObject(this, &UBH_PlayerHintComponent::HandleCombatIdentityAggro);
	EnemyAggroHandle = ABH_EnemyBase::OnAnyEnemyAggroChanged.AddUObject(this, &UBH_PlayerHintComponent::HandleEnemyAggro);
}

void UBH_PlayerHintComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PollTimer);
	}
	if (IdentityAggroHandle.IsValid())
	{
		UBH_CombatIdentityComponent::OnAnyAggroTargetChanged().Remove(IdentityAggroHandle);
		IdentityAggroHandle.Reset();
	}
	if (EnemyAggroHandle.IsValid())
	{
		ABH_EnemyBase::OnAnyEnemyAggroChanged.Remove(EnemyAggroHandle);
		EnemyAggroHandle.Reset();
	}
	Super::EndPlay(EndPlayReason);
}

APawn* UBH_PlayerHintComponent::GetLivingPawn() const
{
	const APlayerState* PS = Cast<APlayerState>(GetOwner());
	APawn* Pawn = PS ? PS->GetPawn() : nullptr;
	return UBH_PlayerDeathComponent::IsLivingPlayer(Pawn) ? Pawn : nullptr;
}

bool UBH_PlayerHintComponent::HasNotFired(const UBH_HintData* Hint) const
{
	return Hint && !Fired.Contains(Hint->HintId);
}

void UBH_PlayerHintComponent::FireHint(FName HintId)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !UBH_HintSettings::Get()->bHintsEnabled || HintId.IsNone() || Fired.Contains(HintId))
	{
		return;
	}
	const APlayerState* PS = Cast<APlayerState>(GetOwner());
	if (!PS || !PS->GetPlayerController() || !UBH_HintLibrary::FindHint(HintId))
	{
		return;
	}
	Fired.Add(HintId);
	ClientShowHint(HintId);
}

void UBH_PlayerHintComponent::FireKey(FName Key)
{
	for (const UBH_HintData* Hint : UBH_HintLibrary::GetAllHints())
	{
		if (Hint && Hint->Trigger == EBH_HintTrigger::Manual && Hint->TriggerKey == Key)
		{
			FireHint(Hint->HintId);
		}
	}
}

void UBH_PlayerHintComponent::ClientShowHint_Implementation(FName HintId)
{
	const APlayerState* PS = Cast<APlayerState>(GetOwner());
	const APlayerController* PC = PS ? PS->GetPlayerController() : nullptr;
	if (!PC || !PC->IsLocalController())
	{
		return;
	}
	if (UBH_HintSubsystem* Hints = UBH_HintSubsystem::ForController(PC))
	{
		Hints->RequestHint(HintId);
	}
}

void UBH_PlayerHintComponent::Poll()
{
	APawn* Pawn = GetLivingPawn();
	if (!Pawn)
	{
		LastPawn.Reset();
		return;
	}

	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	if (LastPawn.Get() != Pawn)
	{
		LastPawn = Pawn;
		PawnAliveSince = Now;
	}
	if (Now - PawnAliveSince < PawnSettleSeconds)
	{
		return;
	}

	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Pawn);
	for (const UBH_HintData* Hint : UBH_HintLibrary::GetAllHints())
	{
		if (!HasNotFired(Hint))
		{
			continue;
		}
		switch (Hint->Trigger)
		{
		case EBH_HintTrigger::PlayerReady:
			FireHint(Hint->HintId);
			break;

		case EBH_HintTrigger::PlayerTag:
			if (ASC && Hint->TriggerTag.IsValid() && ASC->HasMatchingGameplayTag(Hint->TriggerTag))
			{
				FireHint(Hint->HintId);
			}
			break;

		case EBH_HintTrigger::Attribute:
		{
			if (!ASC || !Hint->Attribute.IsValid() || !ASC->HasAttributeSetForAttribute(Hint->Attribute))
			{
				break;
			}
			float Value = ASC->GetNumericAttribute(Hint->Attribute);
			if (Hint->MaxAttribute.IsValid() && ASC->HasAttributeSetForAttribute(Hint->MaxAttribute))
			{
				const float Max = ASC->GetNumericAttribute(Hint->MaxAttribute);
				if (Max <= 0.f)
				{
					break;
				}
				Value /= Max;
			}
			if (Hint->bFireWhenBelow ? (Value > 0.f && Value < Hint->Threshold) : (Value >= Hint->Threshold))
			{
				FireHint(Hint->HintId);
			}
			break;
		}

		case EBH_HintTrigger::PartnerDown:
		{
			const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
			if (!GameState)
			{
				break;
			}
			for (const APlayerState* Other : GameState->PlayerArray)
			{
				if (!Other || Other == GetOwner())
				{
					continue;
				}
				const APawn* OtherPawn = Other->GetPawn();
				const UBH_PlayerDeathComponent* Death = OtherPawn ? OtherPawn->FindComponentByClass<UBH_PlayerDeathComponent>() : nullptr;
				if (Death && Death->IsWindowOpen())
				{
					FireHint(Hint->HintId);
					break;
				}
			}
			break;
		}

		default:
			break;
		}
	}
}

void UBH_PlayerHintComponent::HandleCombatIdentityAggro(UBH_CombatIdentityComponent* Source, AActor* NewTarget)
{
	if (Source)
	{
		HandleEnemyEngaged(Source->Rank, NewTarget);
	}
}

void UBH_PlayerHintComponent::HandleEnemyAggro(ABH_EnemyBase* Enemy, AActor* Target)
{
	if (Enemy)
	{
		HandleEnemyEngaged(Enemy->Rank, Target);
	}
}

void UBH_PlayerHintComponent::HandleEnemyEngaged(EBH_EnemyRank Rank, AActor* Target)
{
	const APawn* TargetPawn = Cast<APawn>(Target);
	if (!TargetPawn || TargetPawn->GetPlayerState() != GetOwner())
	{
		return;
	}
	for (const UBH_HintData* Hint : UBH_HintLibrary::GetAllHints())
	{
		if (HasNotFired(Hint) && Hint->Trigger == EBH_HintTrigger::EnemyEngaged
			&& static_cast<uint8>(Rank) >= static_cast<uint8>(Hint->MinEnemyRank))
		{
			FireHint(Hint->HintId);
		}
	}
}
