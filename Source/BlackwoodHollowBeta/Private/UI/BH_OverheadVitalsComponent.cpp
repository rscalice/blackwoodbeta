// Blackwood Hollow - overhead enemy vitals (implementation)

#include "UI/BH_OverheadVitalsComponent.h"
#include "UI/BH_OverheadVitalsWidget.h"
#include "Combat/BH_LockOnComponent.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarBHOverheadVitalsDebug(
	TEXT("bh.OverheadVitals.Debug"),
	0,
	TEXT("1 = force the overhead enemy vitals bar on every enemy (still hidden when dead or hard-locked). For testing."),
	ECVF_Default);

UBH_OverheadVitalsComponent::UBH_OverheadVitalsComponent()
{
	PrimaryComponentTick.bCanEverTick = true; // the widget component's own tick registers / draws the screen-space widget; visibility is on a timer
	Space = EWidgetSpace::Screen;
	bDrawAtDesiredSize = true;
	SetPivot(FVector2D(0.5, 0.5));
	SetCastShadow(false);
	SetGenerateOverlapEvents(false);
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	bReceivesDecals = false;
}

void UBH_OverheadVitalsComponent::OnRegister()
{
	// Dedicated servers never create the widget: no widget class -> UWidgetComponent::InitWidget has nothing to build.
	const UWorld* World = GetWorld();
	bCosmeticMachine = World && World->GetNetMode() != NM_DedicatedServer && !IsRunningDedicatedServer();
	if (!bCosmeticMachine)
	{
		SetWidgetClass(nullptr);
		SetVisibility(false);
	}
	Super::OnRegister();
}

void UBH_OverheadVitalsComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!bCosmeticMachine)
	{
		SetComponentTickEnabled(false);
		return;
	}

	// Position: HeightAboveCapsuleTop above the top of the owner's capsule (relative to the root it hangs from).
	if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		USceneComponent* Root = Character->GetRootComponent();
		if (Root && GetAttachParent() != Root)
		{
			AttachToComponent(Root, FAttachmentTransformRules::KeepRelativeTransform);
		}
		SetRelativeLocation(FVector(0.0, 0.0, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + HeightAboveCapsuleTop));
	}

	SetVisibility(false); // starts hidden; the first evaluation that wants it shows it and the widget fades in

	if (UWorld* World = GetWorld())
	{
		// Random phase so a crowd of enemies does not evaluate on the same frame.
		World->GetTimerManager().SetTimer(UpdateTimer, this, &UBH_OverheadVitalsComponent::UpdateVisibility,
			FMath::Max(UpdateInterval, 0.02f), true, FMath::FRandRange(0.f, UpdateInterval));
	}
}

void UBH_OverheadVitalsComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(UpdateTimer);
	}
	Super::EndPlay(EndPlayReason);
}

void UBH_OverheadVitalsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bCosmeticMachine)
	{
		return;
	}

	const float Target = bWanted ? 1.f : 0.f;
	if (FMath::IsNearlyEqual(CurrentAlpha, Target, 1.0e-5f))
	{
		if (!bWanted && IsVisible())
		{
			SetVisibility(false); // fade finished: stop drawing
		}
		return;
	}
	CurrentAlpha = FMath::FInterpConstantTo(CurrentAlpha, Target, DeltaTime, 1.f / FMath::Max(bWanted ? FadeInTime : FadeOutTime, 0.01f));
	if (UBH_OverheadVitalsWidget* Bar = Cast<UBH_OverheadVitalsWidget>(GetWidget()))
	{
		Bar->SetOverheadOpacity(CurrentAlpha);
	}
}

// ----------------------------------------------------------------------------
// Hit signal
// ----------------------------------------------------------------------------

void UBH_OverheadVitalsComponent::NotifyHitByLocalPlayer(const AActor* Attacker, const AActor* Victim)
{
	const APawn* AttackerPawn = Cast<APawn>(Attacker);
	if (!AttackerPawn || !Victim || !AttackerPawn->IsLocallyControlled())
	{
		return;
	}
	if (UBH_OverheadVitalsComponent* Bar = Victim->FindComponentByClass<UBH_OverheadVitalsComponent>())
	{
		Bar->MarkHitByLocalPlayer();
	}
}

void UBH_OverheadVitalsComponent::MarkHitByLocalPlayer()
{
	if (const UWorld* World = GetWorld())
	{
		LastLocalHitTime = World->GetTimeSeconds();
	}
}

// ----------------------------------------------------------------------------
// Visibility
// ----------------------------------------------------------------------------

APlayerController* UBH_OverheadVitalsComponent::FindLocalController() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (PC && PC->IsLocalController())
		{
			return PC;
		}
	}
	return nullptr;
}

bool UBH_OverheadVitalsComponent::IsOwnerDead(const UAbilitySystemComponent* ASC) const
{
	if (!ASC)
	{
		return false;
	}
	if (ASC->HasMatchingGameplayTag(TAG_State_Combat_Dead))
	{
		return true;
	}
	// The Dead tag is a loose tag and may not reach clients; Health does.
	return ASC->HasAttributeSetForAttribute(UAH_AttributeSet::GetHealthAttribute())
		&& ASC->GetNumericAttribute(UAH_AttributeSet::GetHealthAttribute()) <= 0.f;
}

void UBH_OverheadVitalsComponent::TryBind()
{
	UBH_OverheadVitalsWidget* Bar = Cast<UBH_OverheadVitalsWidget>(GetWidget());
	UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner());
	if (!Bar || !ASC || !ASC->HasAttributeSetForAttribute(UAH_AttributeSet::GetHealthAttribute()))
	{
		return; // ASC / attribute set not there yet (enemies grant it after spawn): retried on the next evaluation
	}
	if (ASC->GetNumericAttribute(UAH_AttributeSet::GetMaxHealthAttribute()) <= 0.f)
	{
		return; // attributes not initialised yet
	}
	Bar->InitializeHUD(ASC);
	bBound = true;
}

bool UBH_OverheadVitalsComponent::EvaluateWanted() const
{
	const AActor* Owner = GetOwner();
	const UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return false;
	}
	if (IsOwnerDead(UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(const_cast<AActor*>(Owner))))
	{
		return false;
	}

	const APlayerController* PC = FindLocalController();
	if (!PC)
	{
		return false;
	}
	const UBH_LockOnComponent* LockOn = PC->FindComponentByClass<UBH_LockOnComponent>();
	if (LockOn && LockOn->GetLockedTarget() == Owner)
	{
		return false; // hard-locked: the top-centre target vitals shows it
	}
	if (CVarBHOverheadVitalsDebug.GetValueOnGameThread() != 0)
	{
		return true;
	}

	const APawn* LocalPawn = PC->GetPawn();
	const FVector From = LocalPawn ? LocalPawn->GetActorLocation() : PC->GetFocalLocation();
	if (MaxRange > 0.f && FVector::DistSquared(From, Owner->GetActorLocation()) > FMath::Square(MaxRange))
	{
		return false;
	}

	if (LockOn && LockOn->GetSoftTarget() == Owner)
	{
		return true;
	}
	return World->GetTimeSeconds() - LastLocalHitTime < HitLingerTime;
}

void UBH_OverheadVitalsComponent::UpdateVisibility()
{
	UBH_OverheadVitalsWidget* Bar = Cast<UBH_OverheadVitalsWidget>(GetWidget());
	if (!Bar)
	{
		InitWidget();
		Bar = Cast<UBH_OverheadVitalsWidget>(GetWidget());
		if (!Bar)
		{
			return;
		}
	}
	// Hiding the component destructs the widget, and UBH_HUDWidget::NativeDestruct unbinds from the ASC: re-bind when that happened.
	if (bBound && !Bar->GetHUDAbilitySystem())
	{
		bBound = false;
	}
	if (!bBound)
	{
		TryBind();
	}

	bWanted = bBound && EvaluateWanted();
	if (bWanted && !IsVisible())
	{
		SetVisibility(true); // TickComponent fades the opacity in
	}
}
