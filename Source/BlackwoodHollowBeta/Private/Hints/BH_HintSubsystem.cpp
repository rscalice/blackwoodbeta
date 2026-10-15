// Blackwood Hollow - per-local-player hint presenter and Field Notes data (Phase 13E, implementation)

#include "Hints/BH_HintSubsystem.h"
#include "Hints/BH_HintData.h"
#include "Hints/BH_HintLibrary.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Player/BH_PlayerDeathComponent.h"
#include "UI/BH_HUDSubsystem.h"
#include "UI/BH_NotificationSubsystem.h"
#include "UI/BH_UILayoutSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogBHHintSubsystem, Log, All);

namespace
{
	/** A hint that was handed to the queue but never started (a death cleared the queue) is given up after this long and asked for again. */
	constexpr double InFlightTimeoutSeconds = 20.0;
}

UBH_HintSubsystem* UBH_HintSubsystem::ForController(const APlayerController* PC)
{
	const ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
	return LocalPlayer ? LocalPlayer->GetSubsystem<UBH_HintSubsystem>() : nullptr;
}

void UBH_HintSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(TEXT("BHHintQueue"), 0.f, [this](float DeltaTime)
	{
		return Tick(DeltaTime);
	});
}

void UBH_HintSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	TickHandle.Reset();
	if (bBoundNotifications)
	{
		if (UBH_NotificationSubsystem* Notifications = GetNotifications())
		{
			Notifications->OnNotificationShown.RemoveDynamic(this, &UBH_HintSubsystem::HandleNotificationShown);
			Notifications->OnNotificationFinished.RemoveDynamic(this, &UBH_HintSubsystem::HandleNotificationFinished);
		}
		bBoundNotifications = false;
	}
	Waiting.Reset();
	Super::Deinitialize();
}

APlayerController* UBH_HintSubsystem::GetPC() const
{
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	return LocalPlayer ? LocalPlayer->GetPlayerController(LocalPlayer->GetWorld()) : nullptr;
}

UBH_NotificationSubsystem* UBH_HintSubsystem::GetNotifications() const
{
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	return LocalPlayer ? LocalPlayer->GetSubsystem<UBH_NotificationSubsystem>() : nullptr;
}

FString UBH_HintSubsystem::GetSlotName() const
{
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	FString Slot = UBH_HintSettings::Get()->SaveSlotPrefix + FString::FromInt(LocalPlayer ? LocalPlayer->GetLocalPlayerIndex() : 0);
#if WITH_EDITOR
	// Several PIE clients in one process all report controller id 0: give each its own profile.
	if (const UWorld* World = LocalPlayer ? LocalPlayer->GetWorld() : nullptr)
	{
		if (World->WorldType == EWorldType::PIE)
		{
			Slot += FString::Printf(TEXT("_PIE%d"), World->GetOutermost()->GetPIEInstanceID());
		}
	}
#endif
	return Slot;
}

void UBH_HintSubsystem::EnsureLoaded()
{
	if (bLoaded)
	{
		return;
	}
	const FString Slot = GetSlotName();
	if (UGameplayStatics::DoesSaveGameExist(Slot, 0))
	{
		Save = Cast<UBH_HintSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
	}
	if (!Save)
	{
		Save = Cast<UBH_HintSaveGame>(UGameplayStatics::CreateSaveGameObject(UBH_HintSaveGame::StaticClass()));
	}
	bLoaded = true;
	UE_LOG(LogBHHintSubsystem, Log, TEXT("Hint profile '%s': %d seen hint(s)."), *Slot, Save ? Save->SeenHints.Num() : 0);
}

void UBH_HintSubsystem::SaveSeen() const
{
	if (Save)
	{
		UGameplayStatics::SaveGameToSlot(Save, GetSlotName(), 0);
	}
}

bool UBH_HintSubsystem::HasSeenHint(FName HintId) const
{
	const_cast<UBH_HintSubsystem*>(this)->EnsureLoaded();
	return Save && Save->SeenHints.Contains(HintId);
}

TArray<FName> UBH_HintSubsystem::GetSeenHintIds() const
{
	const_cast<UBH_HintSubsystem*>(this)->EnsureLoaded();
	return Save ? Save->SeenHints : TArray<FName>();
}

TArray<UBH_HintData*> UBH_HintSubsystem::GetAllHints() const
{
	return UBH_HintLibrary::GetAllHints();
}

TArray<UBH_HintData*> UBH_HintSubsystem::GetFieldNotes() const
{
	TArray<UBH_HintData*> Result;
	for (UBH_HintData* Hint : UBH_HintLibrary::GetAllHints())
	{
		if (Hint && HasSeenHint(Hint->HintId))
		{
			Result.Add(Hint);
		}
	}
	return Result;
}

UBH_HintData* UBH_HintSubsystem::FindHint(FName HintId) const
{
	return UBH_HintLibrary::FindHint(HintId);
}

void UBH_HintSubsystem::ResetSeenHints()
{
	EnsureLoaded();
	if (Save)
	{
		Save->SeenHints.Reset();
		SaveSeen();
	}
	Waiting.Reset();
	OnFieldNotesChanged.Broadcast();
}

void UBH_HintSubsystem::MarkSeen(FName HintId)
{
	EnsureLoaded();
	if (Save && !Save->SeenHints.Contains(HintId))
	{
		Save->SeenHints.Add(HintId);
		SaveSeen();
		OnFieldNotesChanged.Broadcast();
	}
}

void UBH_HintSubsystem::RequestHint(FName HintId)
{
	if (!UBH_HintSettings::Get()->bHintsEnabled || HintId.IsNone())
	{
		return;
	}
	EnsureLoaded();
	const UBH_HintData* Hint = UBH_HintLibrary::FindHint(HintId);
	if (!Hint)
	{
		UE_LOG(LogBHHintSubsystem, Warning, TEXT("RequestHint: no hint asset with id '%s'."), *HintId.ToString());
		return;
	}
	if (HasSeenHint(HintId) || InFlightId == HintId || Waiting.ContainsByPredicate([HintId](const FWaiting& W) { return W.HintId == HintId; }))
	{
		return;
	}
	FWaiting Entry;
	Entry.HintId = HintId;
	Entry.ReadyTime = Clock + Hint->DelaySeconds;
	Entry.Sequence = NextSequence++;
	Waiting.Add(Entry);
}

bool UBH_HintSubsystem::PrerequisitesMet(const UBH_HintData& Hint) const
{
	for (const FName Required : Hint.RequiresSeen)
	{
		if (!HasSeenHint(Required))
		{
			return false;
		}
	}
	return true;
}

bool UBH_HintSubsystem::IsQuietMoment() const
{
	const APlayerController* PC = GetPC();
	if (!PC || !UBH_PlayerDeathComponent::IsLivingPlayer(PC->GetPawn()))
	{
		return false;
	}
	const UBH_NotificationSubsystem* Notifications = GetNotifications();
	if (!Notifications || !Notifications->HasPresenter() || Notifications->IsSuppressed() || Notifications->IsShowing())
	{
		return false;
	}
	if (const UBH_UILayoutSubsystem* Layout = GetLocalPlayer()->GetSubsystem<UBH_UILayoutSubsystem>())
	{
		if (Layout->IsAnyMenuOpen())
		{
			return false;
		}
	}
	if (const UBH_HUDSubsystem* HUD = GetLocalPlayer()->GetSubsystem<UBH_HUDSubsystem>())
	{
		if (HUD->IsBossIntroActive(UBH_HintSettings::Get()->BossIntroSeconds))
		{
			return false;
		}
	}
	return Clock >= NotBefore;
}

bool UBH_HintSubsystem::Tick(float DeltaTime)
{
	Clock += DeltaTime;

	if (!bBoundNotifications)
	{
		if (UBH_NotificationSubsystem* Notifications = GetNotifications())
		{
			Notifications->OnNotificationShown.AddDynamic(this, &UBH_HintSubsystem::HandleNotificationShown);
			Notifications->OnNotificationFinished.AddDynamic(this, &UBH_HintSubsystem::HandleNotificationFinished);
			bBoundNotifications = true;
		}
	}

	if (!InFlightId.IsNone())
	{
		if (!bInFlightShown && Clock - InFlightSince > InFlightTimeoutSeconds)
		{
			// The queue dropped it (a death clears the queue). Ask again.
			const FName Lost = InFlightId;
			InFlightId = NAME_None;
			RequestHint(Lost);
		}
		return true;
	}

	if (Waiting.IsEmpty() || !IsQuietMoment())
	{
		return true;
	}

	int32 BestIndex = INDEX_NONE;
	int32 BestPriority = 0;
	for (int32 Index = 0; Index < Waiting.Num(); ++Index)
	{
		const FWaiting& Entry = Waiting[Index];
		const UBH_HintData* Hint = UBH_HintLibrary::FindHint(Entry.HintId);
		if (!Hint || Entry.ReadyTime > Clock || !PrerequisitesMet(*Hint))
		{
			continue;
		}
		if (BestIndex == INDEX_NONE || Hint->Priority > BestPriority
			|| (Hint->Priority == BestPriority && Entry.Sequence < Waiting[BestIndex].Sequence))
		{
			BestIndex = Index;
			BestPriority = Hint->Priority;
		}
	}
	if (BestIndex != INDEX_NONE)
	{
		const FName Chosen = Waiting[BestIndex].HintId;
		Waiting.RemoveAt(BestIndex);
		if (const UBH_HintData* Hint = UBH_HintLibrary::FindHint(Chosen))
		{
			ShowHint(*Hint);
		}
	}
	return true;
}

void UBH_HintSubsystem::ShowHint(const UBH_HintData& Hint)
{
	UBH_NotificationSubsystem* Notifications = GetNotifications();
	if (!Notifications)
	{
		return;
	}
	FBH_Notification Notification;
	Notification.Kind = EBH_NotificationKind::Hint;
	Notification.Title = Hint.Title;
	Notification.Body = Hint.Text;
	Notification.HoldSeconds = Hint.HoldSeconds > 0.f ? Hint.HoldSeconds : UBH_HintSettings::Get()->DefaultHoldSeconds;
	Notification.SourceId = Hint.HintId;
	Notification.GlyphActions = Hint.GlyphActions;

	InFlightId = Hint.HintId;
	InFlightSince = Clock;
	bInFlightShown = false;
	Notifications->Enqueue(Notification);
}

void UBH_HintSubsystem::HandleNotificationShown(const FBH_Notification& Notification)
{
	if (Notification.Kind != EBH_NotificationKind::Hint || Notification.SourceId.IsNone() || Notification.SourceId != InFlightId)
	{
		return;
	}
	bInFlightShown = true;
	MarkSeen(InFlightId);
	OnHintShown.Broadcast(UBH_HintLibrary::FindHint(InFlightId));
}

void UBH_HintSubsystem::HandleNotificationFinished()
{
	if (!InFlightId.IsNone() && bInFlightShown)
	{
		InFlightId = NAME_None;
		bInFlightShown = false;
		NotBefore = Clock + UBH_HintSettings::Get()->MinGapSeconds;
	}
}

// -- Console (non-shipping use; harmless in shipping) ----------------------------------------------------------------------

namespace
{
	void ForEachLocalHintSubsystem(UWorld* World, TFunctionRef<void(UBH_HintSubsystem&)> Fn)
	{
		if (!World)
		{
			return;
		}
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			const APlayerController* PC = It->Get();
			if (PC && PC->IsLocalController())
			{
				if (UBH_HintSubsystem* Hints = UBH_HintSubsystem::ForController(PC))
				{
					Fn(*Hints);
				}
			}
		}
	}

	FAutoConsoleCommandWithWorldAndArgs GBHHintFire(
		TEXT("bh.Hint.Fire"),
		TEXT("bh.Hint.Fire <HintId>: ask the local hint presenter to show a hint now (ignores the server trigger; still honours seen/quiet rules)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() < 1)
			{
				return;
			}
			ForEachLocalHintSubsystem(World, [&Args](UBH_HintSubsystem& Hints) { Hints.RequestHint(FName(*Args[0])); });
		}));

	FAutoConsoleCommandWithWorldAndArgs GBHHintReset(
		TEXT("bh.Hint.Reset"),
		TEXT("bh.Hint.Reset: forget every seen hint for the local player(s) in this world."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& /*Args*/, UWorld* World)
		{
			ForEachLocalHintSubsystem(World, [](UBH_HintSubsystem& Hints) { Hints.ResetSeenHints(); });
		}));

	FAutoConsoleCommandWithWorldAndArgs GBHHintList(
		TEXT("bh.Hint.List"),
		TEXT("bh.Hint.List: log every hint and whether the local player has seen it."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& /*Args*/, UWorld* World)
		{
			ForEachLocalHintSubsystem(World, [](UBH_HintSubsystem& Hints)
			{
				for (const UBH_HintData* Hint : Hints.GetAllHints())
				{
					UE_LOG(LogBHHintSubsystem, Display, TEXT("Hint %s: %s"), *Hint->HintId.ToString(), Hints.HasSeenHint(Hint->HintId) ? TEXT("seen") : TEXT("unseen"));
				}
			});
		}));
}
