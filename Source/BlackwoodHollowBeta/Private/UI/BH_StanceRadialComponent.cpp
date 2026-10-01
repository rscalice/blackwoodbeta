// Blackwood Hollow - stance radial menu (implementation)

#include "UI/BH_StanceRadialComponent.h"
#include "Combat/BH_LoadoutComponent.h"
#include "AbilitySystem/BH_CombatFunctionLibrary.h"
#include "RadialSelectorType.h"
#include "RadialSelectorMenuLayout.h"
#include "EnhancedInputComponent.h"
#include "InputAction.h"
#include "Abilities/GameplayAbility.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"

UBH_StanceRadialComponent::UBH_StanceRadialComponent()
{
	StanceDisplayNames.Add(FName(TEXT("SwordAndShield")), NSLOCTEXT("BHStance", "SnS", "Sword & Shield"));
	StanceDisplayNames.Add(FName(TEXT("DualSword")), NSLOCTEXT("BHStance", "DS", "Dual Swords"));
	StanceDisplayNames.Add(FName(TEXT("Greatsword")), NSLOCTEXT("BHStance", "GS", "Greatsword"));

	// Hold-to-open, release-to-confirm (we drive open/close ourselves; see header).
	ActivationMode = ERadialSelectorActivationMode::Hold;
	ConfirmTrigger = ERadialSelectorConfirmTrigger::OnActivationInput;
}

void UBH_StanceRadialComponent::BeginPlay()
{
	OnInputBound.AddUniqueDynamic(this, &UBH_StanceRadialComponent::HandleInputBound);
	OnSegmentSelected.AddUniqueDynamic(this, &UBH_StanceRadialComponent::HandleSegmentSelected);

	// The base component wants MenuData while it initialises; start with the default cycle and rebuild once a pawn is bound.
	if (!MenuData)
	{
		RebuildFromStances({ FName(TEXT("SwordAndShield")), FName(TEXT("DualSword")), FName(TEXT("Greatsword")) });
	}

	Super::BeginPlay();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(PollTimer, this, &UBH_StanceRadialComponent::PollPawn, PawnPollInterval, true, 0.1f);
	}
}

void UBH_StanceRadialComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PollTimer);
	}
	if (BoundLoadout)
	{
		BoundLoadout->OnAvailableStancesChanged.RemoveDynamic(this, &UBH_StanceRadialComponent::HandleStancesChanged);
		BoundLoadout = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

APawn* UBH_StanceRadialComponent::GetControlledPawn() const
{
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	return PC ? PC->GetPawn() : nullptr;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void UBH_StanceRadialComponent::HandleInputBound(UEnhancedInputComponent* InputComponent)
{
	if (InputComponent && HoldOpenAction)
	{
		InputComponent->BindAction(HoldOpenAction, ETriggerEvent::Triggered, this, &UBH_StanceRadialComponent::HandleHoldTriggered);
		InputComponent->BindAction(HoldOpenAction, ETriggerEvent::Completed, this, &UBH_StanceRadialComponent::HandleHoldCompleted);
	}
}

void UBH_StanceRadialComponent::HandleHoldTriggered(const FInputActionValue& Value)
{
	OpenWheel();
}

void UBH_StanceRadialComponent::HandleHoldCompleted(const FInputActionValue& Value)
{
	CloseWheel(/*bConfirm*/ true);
}

void UBH_StanceRadialComponent::OpenWheel()
{
	if (State == ERadialSelectorState::Closed && GetSegmentCount() > 0)
	{
		OpenMenu();
	}
}

void UBH_StanceRadialComponent::CloseWheel(bool bConfirm)
{
	if (State == ERadialSelectorState::Open)
	{
		CloseMenu(/*bInputWasExplicitCancel*/ !bConfirm);
	}
}

void UBH_StanceRadialComponent::DebugHoverSegment(int32 Index)
{
	if (State == ERadialSelectorState::Open && MenuData && MenuData->Segments.IsValidIndex(Index))
	{
		HoveredSegmentIndex = Index;
		OnSegmentHovered.Broadcast(Index);
	}
}

// ---------------------------------------------------------------------------
// Wheel content
// ---------------------------------------------------------------------------

int32 UBH_StanceRadialComponent::GetSegmentCount() const
{
	return MenuData ? MenuData->Segments.Num() : 0;
}

TArray<FName> UBH_StanceRadialComponent::GetSegmentIdentifiers() const
{
	TArray<FName> Out;
	if (MenuData)
	{
		for (const FRadialSelectorSegment& Segment : MenuData->Segments)
		{
			Out.Add(Segment.Identifier);
		}
	}
	return Out;
}

void UBH_StanceRadialComponent::RebuildFromStances(const TArray<FName>& Stances)
{
	// MenuData may only be swapped while the wheel is closed.
	if (State == ERadialSelectorState::Open)
	{
		CloseMenu(/*bInputWasExplicitCancel*/ true, /*bForceFullClose*/ true);
	}

	URadialSelectorMenuData* Data = NewObject<URadialSelectorMenuData>(this, NAME_None, RF_Transient);
	const APawn* Pawn = GetControlledPawn();

	for (const FName& Stance : Stances)
	{
		FRadialSelectorSegment Segment;
		Segment.Identifier = Stance;
		const FText* Friendly = StanceDisplayNames.Find(Stance);
		Segment.DisplayName = Friendly ? *Friendly : FText::FromName(Stance);
		Segment.Icon = UBH_CombatFunctionLibrary::GetStanceIconForPose(Pawn, Stance);
		Data->Segments.Add(Segment);
	}
	URadialSelectorMenuLayout* LayoutToUse = StanceLayout.Get();
	if (!LayoutToUse && MenuData)
	{
		LayoutToUse = MenuData->Layout.Get();
	}
	Data->Layout = LayoutToUse;
	MenuData = Data;
}

// ---------------------------------------------------------------------------
// Stance switching
// ---------------------------------------------------------------------------

bool UBH_StanceRadialComponent::SelectStance(FName Stance)
{
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	APawn* Pawn = GetControlledPawn();
	if (!PC || !Pawn || Stance.IsNone())
	{
		return false;
	}

	const bool bRequested = UBH_CombatFunctionLibrary::RequestStanceByName(Pawn, Stance.ToString());

	// Same fallback chain the Blueprint CycleStance had: data-driven ability, else the Dual/SnS class variables.
	TSubclassOf<UGameplayAbility> AbilityClass = UBH_CombatFunctionLibrary::GetMeleeAbilityForPose(Pawn, Stance);
	if (!AbilityClass)
	{
		const FName FallbackVar = (Stance == FName(TEXT("DualSword"))) ? FName(TEXT("DualSwordAbilityClass")) : FName(TEXT("SwordShieldAbilityClass"));
		if (const FClassProperty* Fallback = CastField<FClassProperty>(PC->GetClass()->FindPropertyByName(FallbackVar)))
		{
			AbilityClass = Cast<UClass>(Fallback->GetObjectPropertyValue_InContainer(PC));
		}
	}
	if (AbilityClass)
	{
		if (const FClassProperty* Target = CastField<FClassProperty>(PC->GetClass()->FindPropertyByName(FName(TEXT("MeleeAttackAbilityClass")))))
		{
			Target->SetObjectPropertyValue_InContainer(PC, AbilityClass.Get());
		}
	}
	return bRequested;
}

void UBH_StanceRadialComponent::CycleStance()
{
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	APawn* Pawn = GetControlledPawn();
	if (!PC || !Pawn)
	{
		return;
	}

	TArray<FString> Cycle;
	if (const UBH_LoadoutComponent* Loadout = UBH_LoadoutComponent::FindLoadoutComponent(Pawn))
	{
		for (const FName& Stance : Loadout->AvailableStances)
		{
			Cycle.Add(Stance.ToString());
		}
	}
	if (Cycle.IsEmpty())
	{
		// No equipment-derived stances (yet): keep the legacy fixed cycle on the controller.
		if (const FArrayProperty* ArrayProp = CastField<FArrayProperty>(PC->GetClass()->FindPropertyByName(FName(TEXT("StanceCycle")))))
		{
			if (CastField<FStrProperty>(ArrayProp->Inner))
			{
				Cycle = *ArrayProp->ContainerPtrToValuePtr<TArray<FString>>(PC);
			}
		}
	}
	if (Cycle.IsEmpty())
	{
		return;
	}

	const int32 Next = UBH_CombatFunctionLibrary::GetNextStanceIndex(Pawn, Cycle);
	if (Cycle.IsValidIndex(Next))
	{
		SelectStance(FName(*Cycle[Next]));
	}
}

void UBH_StanceRadialComponent::HandleSegmentSelected(const FRadialSelectorSegment& SelectedSegment)
{
	SelectStance(SelectedSegment.Identifier);
}

// ---------------------------------------------------------------------------
// Loadout binding
// ---------------------------------------------------------------------------

void UBH_StanceRadialComponent::PollPawn()
{
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (!PC || !PC->IsLocalController())
	{
		return;
	}

	UBH_LoadoutComponent* Loadout = UBH_LoadoutComponent::FindLoadoutComponent(PC->GetPawn());
	if (Loadout == BoundLoadout)
	{
		return;
	}

	if (BoundLoadout)
	{
		BoundLoadout->OnAvailableStancesChanged.RemoveDynamic(this, &UBH_StanceRadialComponent::HandleStancesChanged);
	}
	BoundLoadout = Loadout;
	if (BoundLoadout)
	{
		BoundLoadout->OnAvailableStancesChanged.AddUniqueDynamic(this, &UBH_StanceRadialComponent::HandleStancesChanged);
		HandleStancesChanged(BoundLoadout->AvailableStances);
	}
}

void UBH_StanceRadialComponent::HandleStancesChanged(const TArray<FName>& Stances)
{
	RebuildFromStances(Stances);

	// Current stance no longer available -> first available one.
	const APawn* Pawn = GetControlledPawn();
	const FString Pose = UBH_CombatFunctionLibrary::GetCurrentOverlayPoseDisplayName(Pawn);
	if (Stances.Num() > 0 && !Pose.IsEmpty() && !Stances.Contains(FName(*Pose)))
	{
		SelectStance(Stances[0]);
	}
}
