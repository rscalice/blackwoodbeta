// Blackwood Hollow - the player's side of interaction (implementation)

#include "Interaction/BH_InteractorComponent.h"
#include "Interaction/BH_InteractableComponent.h"
#include "Interaction/BH_InteractionSubsystem.h"
#include "Interaction/BH_InteractPromptWidget.h"
#include "Consumables/BH_ConsumableLibrary.h"
#include "Components/BPC_HeartFragment.h"
#include "Loot/BH_LootLibrary.h"
#include "Player/BH_PlayerDeathComponent.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Blueprint/UserWidget.h"
#include "Components/PostProcessComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "Materials/MaterialInterface.h"
#include "NarrativeItem.h"
#include "Misc/PackageName.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogBHInteractor, Log, All);

UBH_InteractorComponent::UBH_InteractorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false; // only the server ticks, and only while a hold is running
	SetIsReplicatedByDefault(true);
}

UBH_InteractorComponent* UBH_InteractorComponent::Find(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UBH_InteractorComponent>() : nullptr;
}

void UBH_InteractorComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UBH_InteractorComponent, HoldPercent, COND_OwnerOnly);
}

void UBH_InteractorComponent::BeginPlay()
{
	Super::BeginPlay();

	UWorld* ComponentWorld = GetWorld();
	if (ComponentWorld && ComponentWorld->GetNetMode() != NM_DedicatedServer)
	{
		// Every player pawn and every enemy (they share ABH_CharacterBase) gets here, so the scan itself early-outs unless this is a local PLAYER pawn.
		// Random start offset so several local pawns do not all scan on the same frame.
		ComponentWorld->GetTimerManager().SetTimer(ScanTimer, this, &UBH_InteractorComponent::ScanForFocus, ScanInterval, true, FMath::FRandRange(0.f, ScanInterval));
	}
}

void UBH_InteractorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* ComponentWorld = GetWorld())
	{
		ComponentWorld->GetTimerManager().ClearTimer(ScanTimer);
	}

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		EndActive(false);
	}

	bLocalHoldInput = false; // no RPC during teardown
	ApplyFocus(nullptr, false, FText::GetEmpty());
	RemoveInteractContext();

	if (OutlinePPComp)
	{
		OutlinePPComp->DestroyComponent();
		OutlinePPComp = nullptr;
	}
	if (PromptWidget)
	{
		PromptWidget->RemoveFromParent();
		PromptWidget = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

// ============================================================================
// Local: focus scan
// ============================================================================

APlayerController* UBH_InteractorComponent::GetLocalPC() const
{
	const APawn* PawnOwner = Cast<APawn>(GetOwner());
	APlayerController* PC = PawnOwner ? Cast<APlayerController>(PawnOwner->GetController()) : nullptr;
	return (PC && PC->IsLocalController()) ? PC : nullptr;
}

bool UBH_InteractorComponent::IsLocalPlayerPawn() const
{
	return GetLocalPC() != nullptr;
}

void UBH_InteractorComponent::ScanForFocus()
{
	APlayerController* PC = GetLocalPC();
	APawn* PawnOwner = Cast<APawn>(GetOwner());
	UWorld* ComponentWorld = GetWorld();
	if (!PC || !PawnOwner || !ComponentWorld)
	{
		// Not (or no longer) a local player pawn: make sure nothing of ours is left behind.
		if (FocusedInteractable.IsValid() || bInteractContextActive)
		{
			ApplyFocus(nullptr, false, FText::GetEmpty());
		}
		return;
	}

	const UBH_InteractionSubsystem* Registry = ComponentWorld->GetSubsystem<UBH_InteractionSubsystem>();
	const UBH_PlayerDeathComponent* Death = UBH_PlayerDeathComponent::Find(PawnOwner);

	// Downed / dead players interact with nothing, and a downed party member in revive range owns the interact key.
	if (!Registry || !UBH_PlayerDeathComponent::IsLivingPlayer(PawnOwner) || (Death && Death->FindReviveCandidate() != nullptr))
	{
		ApplyFocus(nullptr, false, FText::GetEmpty());
		return;
	}

	const FVector PawnLocation = PawnOwner->GetActorLocation();
	const FVector ViewDir2D = PC->GetControlRotation().Vector().GetSafeNormal2D();
	UBH_InteractableComponent* Current = FocusedInteractable.Get();

	UBH_InteractableComponent* Best = nullptr;
	float BestScore = TNumericLimits<float>::Max();
	bool bBestCan = false;
	FText BestReason;

	for (const TWeakObjectPtr<UBH_InteractableComponent>& Weak : Registry->GetAll())
	{
		UBH_InteractableComponent* Candidate = Weak.Get();
		const AActor* CandidateOwner = Candidate ? Candidate->GetOwner() : nullptr;
		if (!CandidateOwner || CandidateOwner->IsHidden())
		{
			continue;
		}

		const bool bIsCurrent = (Candidate == Current);
		const bool bHolding = bIsCurrent && bLocalHoldInput;

		// A little hysteresis so the focus does not flicker at the range edge.
		const float Distance = Candidate->GetDistanceFrom(PawnLocation);
		const float Limit = Candidate->InteractRange + (bIsCurrent ? 30.f : 0.f);
		if (Distance > Limit)
		{
			continue;
		}

		if (!bHolding && Distance > FacingIgnoreDistance)
		{
			const FVector ToCandidate2D = (Candidate->GetFocusPoint() - PawnLocation).GetSafeNormal2D();
			if (FVector::DotProduct(ViewDir2D, ToCandidate2D) < FacingMinDot)
			{
				continue;
			}
		}

		FText Reason;
		const bool bCan = Candidate->EvaluateCanInteract(PawnOwner, Reason);
		if (!bCan && Reason.IsEmpty())
		{
			continue; // owner wants it not offered at all
		}

		// Nearest wins; the current focus gets a bonus; a usable one beats a greyed-out one.
		const float Score = Distance - (bIsCurrent ? 40.f : 0.f) + (bCan ? 0.f : 500.f);
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = Candidate;
			bBestCan = bCan;
			BestReason = Reason;
		}
	}

	ApplyFocus(Best, bBestCan, BestReason);
}

void UBH_InteractorComponent::ApplyFocus(UBH_InteractableComponent* NewFocus, bool bNewCanInteract, const FText& NewDenyReason)
{
	UBH_InteractableComponent* Old = FocusedInteractable.Get();
	const bool bFocusChanged = (Old != NewFocus);
	const bool bStateChanged = bFocusChanged || bFocusCanInteract != bNewCanInteract || !FocusDenyReason.EqualTo(NewDenyReason);
	if (!bStateChanged)
	{
		return;
	}

	if (bFocusChanged)
	{
		if (bLocalHoldInput)
		{
			// Walked / looked away mid-hold: stop the server's hold now.
			bLocalHoldInput = false;
			ServerEndInteract();
		}
		if (Old)
		{
			Old->SetLocalFocus(false);
		}
		FocusedInteractable = NewFocus;
		if (NewFocus)
		{
			NewFocus->SetLocalFocus(true);
		}
	}

	bFocusCanInteract = NewFocus ? bNewCanInteract : false;
	FocusDenyReason = NewFocus ? NewDenyReason : FText::GetEmpty();

	SetOutlineActive(NewFocus != nullptr);
	UpdateInputContext(NewFocus != nullptr && bFocusCanInteract);
	RefreshPrompt();
	OnFocusChanged.Broadcast(NewFocus, bFocusCanInteract);
}

// ============================================================================
// Local: prompt, outline, input context
// ============================================================================

void UBH_InteractorComponent::EnsurePromptWidget()
{
	if (PromptWidget || bTriedLoadPrompt)
	{
		return;
	}
	bTriedLoadPrompt = true;

	const FSoftObjectPath ClassPath = PromptWidgetClass.ToSoftObjectPath();
	if (ClassPath.IsNull())
	{
		return;
	}
	// The widget Blueprint is a post-build asset: stay quiet-ish (one log line) and keep working without it if it is not there yet.
	if (!FPackageName::DoesPackageExist(ClassPath.GetLongPackageName()))
	{
		UE_LOG(LogBHInteractor, Log, TEXT("%s: prompt widget %s does not exist yet, no on-screen interaction prompt."), *GetNameSafe(GetOwner()), *ClassPath.ToString());
		return;
	}

	APlayerController* PC = GetLocalPC();
	UClass* LoadedClass = PromptWidgetClass.LoadSynchronous();
	if (PC && LoadedClass)
	{
		PromptWidget = CreateWidget<UBH_InteractPromptWidget>(PC, LoadedClass);
		if (PromptWidget)
		{
			PromptWidget->AddToViewport(PromptZOrder);
			PromptWidget->HidePrompt();
		}
	}
}

void UBH_InteractorComponent::RefreshPrompt()
{
	UBH_InteractableComponent* Focus = FocusedInteractable.Get();
	if (!Focus)
	{
		if (PromptWidget)
		{
			PromptWidget->HidePrompt();
		}
		return;
	}

	EnsurePromptWidget();
	if (PromptWidget)
	{
		const APawn* PawnOwner = Cast<APawn>(GetOwner());
		PromptWidget->SetPrompt(Focus->PromptName, Focus->GetPromptActionFor(PawnOwner), bFocusCanInteract, FocusDenyReason, Focus->HoldSeconds > 0.f);
		PromptWidget->SetHoldProgress(GetHoldProgress());
	}
}

void UBH_InteractorComponent::SetOutlineActive(bool bActive)
{
	if (bActive && !OutlinePPComp && !bTriedLoadOutline)
	{
		bTriedLoadOutline = true;
		UMaterialInterface* Material = OutlineMaterial.LoadSynchronous();
		APawn* PawnOwner = Cast<APawn>(GetOwner());
		if (Material && PawnOwner)
		{
			// Same pattern as the lock-on outline: the post process component lives on the pawn (a hidden controller never contributes).
			OutlinePPComp = NewObject<UPostProcessComponent>(PawnOwner, TEXT("InteractOutlinePP"));
			OutlinePPComp->bUnbound = true;
			OutlinePPComp->Priority = 11.f;
			OutlinePPComp->BlendWeight = 1.f;
			OutlinePPComp->AddOrUpdateBlendable(Material, 1.f);
			if (PawnOwner->GetRootComponent())
			{
				OutlinePPComp->SetupAttachment(PawnOwner->GetRootComponent());
			}
			OutlinePPComp->RegisterComponent();
		}
		else if (!Material)
		{
			UE_LOG(LogBHInteractor, Log, TEXT("%s: outline material %s could not be loaded, no interaction outline."), *GetNameSafe(GetOwner()), *OutlineMaterial.ToString());
		}
	}
	if (OutlinePPComp)
	{
		OutlinePPComp->bEnabled = bActive;
	}
}

void UBH_InteractorComponent::UpdateInputContext(bool bWantContext)
{
	UEnhancedInputLocalPlayerSubsystem* CurrentSubsystem = nullptr;
	if (bWantContext)
	{
		const APlayerController* PC = GetLocalPC();
		const ULocalPlayer* LocalPlayerInfo = PC ? PC->GetLocalPlayer() : nullptr;
		CurrentSubsystem = LocalPlayerInfo ? LocalPlayerInfo->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
		bWantContext = CurrentSubsystem != nullptr;
	}

	if (bInteractContextActive && (!bWantContext || InteractContextSubsystem.Get() != CurrentSubsystem))
	{
		RemoveInteractContext();
	}
	if (bWantContext && !bInteractContextActive)
	{
		AddInteractContext(CurrentSubsystem);
	}
}

void UBH_InteractorComponent::AddInteractContext(UEnhancedInputLocalPlayerSubsystem* InputSubsystem)
{
	if (!InputSubsystem || bInteractContextActive)
	{
		return;
	}
	UInputMappingContext* ContextAsset = InteractMappingContext.LoadSynchronous();
	if (!ContextAsset)
	{
		if (!bWarnedMissingContext)
		{
			bWarnedMissingContext = true;
			UE_LOG(LogBHInteractor, Warning, TEXT("%s: InteractMappingContext (%s) could not be loaded, interact input will not take priority."), *GetNameSafe(GetOwner()), *InteractMappingContext.ToString());
		}
		return;
	}

	// Same rule as the revive context: a held gamepad X must not start an interaction (or Shield Bash) by accident when the context changes.
	const ULocalPlayer* LocalPlayerInfo = InputSubsystem->GetLocalPlayer();
	const APlayerController* PC = LocalPlayerInfo ? LocalPlayerInfo->GetPlayerController(GetWorld()) : nullptr;
	FModifyContextOptions ContextOptions;
	ContextOptions.bIgnoreAllPressedKeysUntilRelease = PC && PC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Left);

	InputSubsystem->AddMappingContext(ContextAsset, InteractMappingPriority, ContextOptions);
	InteractContextSubsystem = InputSubsystem;
	InteractContextAsset = ContextAsset;
	bInteractContextActive = true;
}

void UBH_InteractorComponent::RemoveInteractContext()
{
	if (!bInteractContextActive)
	{
		return;
	}
	bInteractContextActive = false;

	UEnhancedInputLocalPlayerSubsystem* InputSubsystem = InteractContextSubsystem.Get();
	UInputMappingContext* ContextAsset = InteractContextAsset.Get();
	InteractContextSubsystem.Reset();
	InteractContextAsset.Reset();
	if (!InputSubsystem || !ContextAsset)
	{
		return;
	}

	// X still held when the context goes: ignore it until release so it does not fire Shield Bash.
	const ULocalPlayer* LocalPlayerInfo = InputSubsystem->GetLocalPlayer();
	const APlayerController* PC = LocalPlayerInfo ? LocalPlayerInfo->GetPlayerController(GetWorld()) : nullptr;
	FModifyContextOptions ContextOptions;
	ContextOptions.bIgnoreAllPressedKeysUntilRelease = PC && PC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Left);

	InputSubsystem->RemoveMappingContext(ContextAsset, ContextOptions);
}

// ============================================================================
// Input
// ============================================================================

void UBH_InteractorComponent::OnInteractInputPressed()
{
	APawn* PawnOwner = Cast<APawn>(GetOwner());
	if (!IsLocalPlayerPawn() || !PawnOwner)
	{
		return;
	}
	// A downed party member in range owns the key (the death component's handler runs from the same binding).
	const UBH_PlayerDeathComponent* Death = UBH_PlayerDeathComponent::Find(PawnOwner);
	if (Death && Death->FindReviveCandidate() != nullptr)
	{
		return;
	}

	UBH_InteractableComponent* Focus = FocusedInteractable.Get();
	if (!Focus || !bFocusCanInteract)
	{
		return;
	}
	bLocalHoldInput = true;
	ServerBeginInteract(Focus);
}

void UBH_InteractorComponent::OnInteractInputReleased()
{
	if (!bLocalHoldInput)
	{
		return;
	}
	bLocalHoldInput = false;
	ServerEndInteract();
}

// ============================================================================
// Server: validation + hold
// ============================================================================

float UBH_InteractorComponent::ReadHealth() const
{
	const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner());
	if (ASC && ASC->HasAttributeSetForAttribute(UAH_AttributeSet::GetHealthAttribute()))
	{
		return ASC->GetNumericAttribute(UAH_AttributeSet::GetHealthAttribute());
	}
	return -1.f;
}

bool UBH_InteractorComponent::ValidateTarget(const UBH_InteractableComponent* Target) const
{
	const APawn* PawnOwner = Cast<APawn>(GetOwner());
	if (!PawnOwner || !Target || !Target->GetOwner() || Target->GetWorld() != GetWorld())
	{
		return false;
	}
	if (!Cast<APlayerController>(PawnOwner->GetController()) || !UBH_PlayerDeathComponent::IsLivingPlayer(PawnOwner))
	{
		return false;
	}
	if (Target->GetDistanceFrom(PawnOwner->GetActorLocation()) > Target->InteractRange + Target->ServerRangeTolerance)
	{
		return false;
	}
	FText Reason;
	return Target->EvaluateCanInteract(PawnOwner, Reason);
}

bool UBH_InteractorComponent::ServerBeginInteract_Validate(UBH_InteractableComponent* Target)
{
	return true;
}

void UBH_InteractorComponent::ServerBeginInteract_Implementation(UBH_InteractableComponent* Target)
{
	APawn* PawnOwner = Cast<APawn>(GetOwner());
	if (!PawnOwner)
	{
		return;
	}
	// A new press replaces any hold that is still running.
	if (ActiveInteractable.IsValid())
	{
		EndActive(false);
	}
	if (!ValidateTarget(Target))
	{
		return;
	}

	if (Target->HoldSeconds <= KINDA_SMALL_NUMBER)
	{
		Target->NotifyInteractionCompleted(PawnOwner);
		return;
	}

	ActiveInteractable = Target;
	ActiveElapsed = 0.f;
	ActiveLastHealth = ReadHealth();
	SetHoldPercentServer(0);
	SetComponentTickEnabled(true);
}

void UBH_InteractorComponent::ServerEndInteract_Implementation()
{
	EndActive(false);
}

void UBH_InteractorComponent::CancelActiveInteraction()
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		EndActive(false);
	}
}

void UBH_InteractorComponent::EndActive(bool bCompleted)
{
	UBH_InteractableComponent* Active = ActiveInteractable.Get();
	APawn* PawnOwner = Cast<APawn>(GetOwner());

	ActiveInteractable.Reset();
	ActiveElapsed = 0.f;
	SetHoldPercentServer(0);
	SetComponentTickEnabled(false);

	if (bCompleted && Active && PawnOwner)
	{
		Active->NotifyInteractionCompleted(PawnOwner);
	}
}

void UBH_InteractorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UBH_InteractableComponent* Active = ActiveInteractable.Get();
	if (!Active || !GetOwner() || !GetOwner()->HasAuthority())
	{
		EndActive(false);
		return;
	}
	if (!ValidateTarget(Active))
	{
		EndActive(false);
		return;
	}

	if (bCancelHoldOnDamage)
	{
		const float Health = ReadHealth();
		if (Health >= 0.f && ActiveLastHealth >= 0.f && Health < ActiveLastHealth - 0.01f)
		{
			EndActive(false);
			return;
		}
		ActiveLastHealth = Health;
	}

	ActiveElapsed += DeltaTime;
	if (ActiveElapsed >= Active->HoldSeconds)
	{
		EndActive(true);
		return;
	}
	const int32 Percent = FMath::Clamp(FMath::FloorToInt((ActiveElapsed / Active->HoldSeconds) * 100.f), 0, 100);
	SetHoldPercentServer(static_cast<uint8>(Percent));
}

void UBH_InteractorComponent::SetHoldPercentServer(uint8 NewPercent)
{
	if (HoldPercent == NewPercent)
	{
		return;
	}
	HoldPercent = NewPercent;

	// OnRep never runs on the machine that set the value: a listen-server host that is also this pawn's owner updates its own UI here.
	const APawn* PawnOwner = Cast<APawn>(GetOwner());
	if (PawnOwner && PawnOwner->IsLocallyControlled())
	{
		OnRep_HoldPercent();
	}
}

void UBH_InteractorComponent::OnRep_HoldPercent()
{
	const float Fraction = GetHoldProgress();
	if (PromptWidget)
	{
		PromptWidget->SetHoldProgress(Fraction);
	}
	OnHoldProgressChanged.Broadcast(Fraction);
}

// ============================================================================
// Grant toast + debug
// ============================================================================

void UBH_InteractorComponent::NotifyItemGranted(const FText& ItemName, int32 Quantity)
{
	const APawn* PawnOwner = Cast<APawn>(GetOwner());
	if (!PawnOwner || Quantity <= 0)
	{
		return;
	}
	if (PawnOwner->IsLocallyControlled())
	{
		OnItemGranted.Broadcast(ItemName, Quantity);
	}
	else if (Cast<APlayerController>(PawnOwner->GetController()))
	{
		ClientItemGranted(ItemName, Quantity);
	}
}

void UBH_InteractorComponent::ClientItemGranted_Implementation(const FText& ItemName, int32 Quantity)
{
	OnItemGranted.Broadcast(ItemName, Quantity);
}

bool UBH_InteractorComponent::ServerDebugGiveItem_Validate(const FString& ItemName, int32 Count)
{
#if UE_BUILD_SHIPPING
	return false;
#else
	return Count >= 1 && Count <= 999;
#endif
}

void UBH_InteractorComponent::ServerDebugGiveItem_Implementation(const FString& ItemName, int32 Count)
{
#if !UE_BUILD_SHIPPING
	const APawn* PawnOwner = Cast<APawn>(GetOwner());
	TSubclassOf<UNarrativeItem> ItemClass = UBH_LootLibrary::ResolveItemClass(ItemName);
	if (!PawnOwner || !ItemClass)
	{
		UE_LOG(LogBHInteractor, Warning, TEXT("bh.Loot.Give: unknown item '%s' (use Shard, Sap, Incense or a class path)."), *ItemName);
		return;
	}
	const int32 Given = UBH_LootLibrary::GrantItem(PawnOwner->GetPlayerState(), ItemClass, Count);
	UE_LOG(LogBHInteractor, Log, TEXT("bh.Loot.Give: %s x%d -> %d granted to %s."), *ItemName, Count, Given, *GetNameSafe(PawnOwner->GetPlayerState()));
#endif
}

bool UBH_InteractorComponent::ServerUseConsumable_Validate(TSubclassOf<UNarrativeItem> ItemClass)
{
	return ItemClass.Get() != nullptr;
}

void UBH_InteractorComponent::ServerUseConsumable_Implementation(TSubclassOf<UNarrativeItem> ItemClass)
{
	APawn* PawnOwner = Cast<APawn>(GetOwner());
	if (!UBH_ConsumableLibrary::ActivateOnServer(PawnOwner, ItemClass))
	{
		UE_LOG(LogBHInteractor, Log, TEXT("ServerUseConsumable: %s could not use %s right now."), *GetNameSafe(PawnOwner), *GetNameSafe(ItemClass));
	}
}

bool UBH_InteractorComponent::ServerDebugSetBlight_Validate(float Value)
{
#if UE_BUILD_SHIPPING
	return false;
#else
	return Value >= 0.f && Value <= 100.f;
#endif
}

void UBH_InteractorComponent::ServerDebugSetBlight_Implementation(float Value)
{
#if !UE_BUILD_SHIPPING
	const APawn* PawnOwner = Cast<APawn>(GetOwner());
	UBPC_HeartFragment* Heart = PawnOwner ? PawnOwner->FindComponentByClass<UBPC_HeartFragment>() : nullptr;
	if (!Heart)
	{
		UE_LOG(LogBHInteractor, Warning, TEXT("bh.Blight.Set: %s has no Heart-Fragment component."), *GetNameSafe(PawnOwner));
		return;
	}
	Heart->DebugSetBlightBuildup(Value);
	UE_LOG(LogBHInteractor, Log, TEXT("bh.Blight.Set: %s Blight meter -> %.1f."), *GetNameSafe(PawnOwner), Value);
#else
	(void)Value;
#endif
}

bool UBH_InteractorComponent::ServerDebugCompleteInteraction_Validate(UBH_InteractableComponent* ClientFocus)
{
#if UE_BUILD_SHIPPING
	return false;
#else
	return true;
#endif
}

void UBH_InteractorComponent::ServerDebugCompleteInteraction_Implementation(UBH_InteractableComponent* ClientFocus)
{
#if !UE_BUILD_SHIPPING
	APawn* PawnOwner = Cast<APawn>(GetOwner());
	UWorld* ComponentWorld = GetWorld();
	const UBH_InteractionSubsystem* Registry = ComponentWorld ? ComponentWorld->GetSubsystem<UBH_InteractionSubsystem>() : nullptr;
	if (!PawnOwner || !Registry)
	{
		return;
	}

	UBH_InteractableComponent* Target = (ClientFocus && ValidateTarget(ClientFocus)) ? ClientFocus : nullptr;
	if (!Target)
	{
		// No usable focus from the caller (a host typing for another player index, a Python call): the nearest interactable this pawn can use.
		float BestDistance = TNumericLimits<float>::Max();
		for (const TWeakObjectPtr<UBH_InteractableComponent>& Weak : Registry->GetAll())
		{
			UBH_InteractableComponent* Candidate = Weak.Get();
			if (!Candidate || !ValidateTarget(Candidate))
			{
				continue;
			}
			const float Distance = Candidate->GetDistanceFrom(PawnOwner->GetActorLocation());
			if (Distance < BestDistance)
			{
				BestDistance = Distance;
				Target = Candidate;
			}
		}
	}

	if (!Target)
	{
		UE_LOG(LogBHInteractor, Warning, TEXT("bh.Interact.Complete: %s has nothing to interact with in range."), *GetNameSafe(PawnOwner));
		return;
	}
	UE_LOG(LogBHInteractor, Log, TEXT("bh.Interact.Complete: %s completes '%s'."), *GetNameSafe(PawnOwner), *GetNameSafe(Target->GetOwner()));
	Target->NotifyInteractionCompleted(PawnOwner);
#else
	(void)ClientFocus;
#endif
}

bool UBH_InteractorComponent::ServerDebugResetLoot_Validate()
{
#if UE_BUILD_SHIPPING
	return false;
#else
	return true;
#endif
}

void UBH_InteractorComponent::ServerDebugResetLoot_Implementation()
{
#if !UE_BUILD_SHIPPING
	UWorld* ComponentWorld = GetWorld();
	const UBH_InteractionSubsystem* Registry = ComponentWorld ? ComponentWorld->GetSubsystem<UBH_InteractionSubsystem>() : nullptr;
	if (!Registry)
	{
		return;
	}
	// Copy: a reset may spawn / destroy things that touch the registry.
	const TArray<TWeakObjectPtr<UBH_InteractableComponent>> Snapshot = Registry->GetAll();
	int32 Count = 0;
	for (const TWeakObjectPtr<UBH_InteractableComponent>& Weak : Snapshot)
	{
		if (UBH_InteractableComponent* Interactable = Weak.Get())
		{
			Interactable->DebugReset();
			++Count;
		}
	}
	UE_LOG(LogBHInteractor, Log, TEXT("bh.Loot.ResetContainers: reset %d interactables."), Count);
#endif
}
