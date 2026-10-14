// Blackwood Hollow - per-local-player notification queue (implementation, Phase 13C)

#include "UI/BH_NotificationSubsystem.h"
#include "UI/BH_HUDSettings.h"
#include "UI/BH_LevelUpBannerWidget.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"

UBH_NotificationSubsystem* UBH_NotificationSubsystem::ForController(const APlayerController* PC)
{
	const ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
	return LocalPlayer ? LocalPlayer->GetSubsystem<UBH_NotificationSubsystem>() : nullptr;
}

void UBH_NotificationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(TEXT("BHNotificationQueue"), 0.f, [this](float DeltaTime)
	{
		return Tick(DeltaTime);
	});
}

void UBH_NotificationSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	TickHandle.Reset();
	Pending.Reset();
	bHasCurrent = false;
	Super::Deinitialize();
}

float UBH_NotificationSubsystem::GetHoldFor(const FBH_Notification& Notification) const
{
	if (Notification.HoldSeconds > 0.f)
	{
		return Notification.HoldSeconds;
	}
	const UBH_HUDSettings* S = UBH_HUDSettings::Get();
	switch (Notification.Kind)
	{
	case EBH_NotificationKind::Death: return S->DeathHoldSeconds;
	case EBH_NotificationKind::LevelUp: return S->LevelUpHoldSeconds;
	case EBH_NotificationKind::Quest: return S->QuestHoldSeconds;
	case EBH_NotificationKind::Discovery: return S->DiscoveryHoldSeconds;
	case EBH_NotificationKind::Loot:
	case EBH_NotificationKind::Experience: return S->LootHoldSeconds;
	default: return S->NoticeHoldSeconds;
	}
}

void UBH_NotificationSubsystem::RebuildLootBody(FBH_Notification& Notification)
{
	TArray<FText> Lines;
	for (const FBH_LootLine& Line : Notification.LootLines)
	{
		Lines.Add(FText::Format(NSLOCTEXT("BlackwoodHollow", "LootLine", "+{0} {1}"), FText::AsNumber(Line.Quantity), Line.ItemName));
	}
	Notification.Body = FText::Join(FText::FromString(TEXT("\n")), Lines);
}

bool UBH_NotificationSubsystem::MergeLootLines(FBH_Notification& Target, const TArray<FBH_LootLine>& Lines, int32 MaxLines)
{
	// Count the lines that would be new first, so a merge never half-applies.
	int32 NewLines = 0;
	for (const FBH_LootLine& Line : Lines)
	{
		const bool bKnown = Target.LootLines.ContainsByPredicate([&Line](const FBH_LootLine& Existing) { return Existing.ItemName.EqualTo(Line.ItemName); });
		NewLines += bKnown ? 0 : 1;
	}
	if (Target.LootLines.Num() + NewLines > MaxLines)
	{
		return false;
	}
	for (const FBH_LootLine& Line : Lines)
	{
		FBH_LootLine* Existing = Target.LootLines.FindByPredicate([&Line](const FBH_LootLine& Candidate) { return Candidate.ItemName.EqualTo(Line.ItemName); });
		if (Existing)
		{
			Existing->Quantity += Line.Quantity;
		}
		else
		{
			Target.LootLines.Add(Line);
		}
	}
	RebuildLootBody(Target);
	return true;
}

void UBH_NotificationSubsystem::Enqueue(const FBH_Notification& Notification)
{
	const UBH_HUDSettings* S = UBH_HUDSettings::Get();
	const bool bIsDeath = Notification.Kind == EBH_NotificationKind::Death;
	if (bSuppressed && !bIsDeath)
	{
		return;
	}

	if (bIsDeath)
	{
		// Death replaces whatever is showing and drops everything else.
		Pending.Reset();
		if (bHasCurrent && Current.Kind != EBH_NotificationKind::Death)
		{
			FinishCurrent();
		}
		GapRemaining = 0.f;
	}

	const bool bBatched = Notification.Kind == EBH_NotificationKind::Loot || Notification.Kind == EBH_NotificationKind::Experience;
	if (bBatched)
	{
		// 1. Merge into the toast that is showing and still absorbing.
		if (bHasCurrent && Current.Kind == Notification.Kind && !bEnding && CurrentAge < S->LootMaxMergeAge
			&& MergeLootLines(Current, Notification.LootLines, S->LootMaxLines))
		{
			CurrentElapsed = FMath::Min(CurrentElapsed, S->NotificationFadeIn); // hold restarts
			OnNotificationUpdated.Broadcast(Current);
			return;
		}
		// 2. Merge into a loot toast that is still waiting.
		for (FPending& Entry : Pending)
		{
			if (Entry.Notification.Kind == Notification.Kind && MergeLootLines(Entry.Notification, Notification.LootLines, S->LootMaxLines))
			{
				return;
			}
		}
	}
	else if (Notification.Kind == EBH_NotificationKind::Notice)
	{
		// The same notice twice in a row (a chest with three items that do not fit) shows once.
		if ((bHasCurrent && Current.Kind == EBH_NotificationKind::Notice && Current.Title.EqualTo(Notification.Title) && Current.Body.EqualTo(Notification.Body))
			|| Pending.ContainsByPredicate([&Notification](const FPending& Entry)
			{
				return Entry.Notification.Kind == EBH_NotificationKind::Notice && Entry.Notification.Title.EqualTo(Notification.Title) && Entry.Notification.Body.EqualTo(Notification.Body);
			}))
		{
			return;
		}
	}

	FPending Entry;
	Entry.Notification = Notification;
	Entry.Sequence = NextSequence++;
	Entry.ReadyTime = Clock + (bBatched ? S->LootBatchWindow : 0.0);
	Pending.Add(MoveTemp(Entry));
}

void UBH_NotificationSubsystem::PushLoot(FText ItemName, int32 Quantity)
{
	if (Quantity <= 0)
	{
		return;
	}
	FBH_Notification N;
	N.Kind = EBH_NotificationKind::Loot;
	FBH_LootLine Line;
	Line.ItemName = ItemName;
	Line.Quantity = Quantity;
	N.LootLines.Add(Line);
	RebuildLootBody(N);
	Enqueue(N);
}

void UBH_NotificationSubsystem::PushExperience(int32 Amount)
{
	if (Amount <= 0)
	{
		return;
	}
	FBH_Notification N;
	N.Kind = EBH_NotificationKind::Experience;
	FBH_LootLine Line;
	Line.ItemName = NSLOCTEXT("BlackwoodHollow", "XPLabel", "XP");
	Line.Quantity = Amount;
	N.LootLines.Add(Line);
	RebuildLootBody(N);
	Enqueue(N);
}

void UBH_NotificationSubsystem::PushNotice(FText Title, FText Body, bool bWarning)
{
	FBH_Notification N;
	N.Kind = EBH_NotificationKind::Notice;
	N.Title = Title;
	N.Body = Body;
	N.bWarning = bWarning;
	Enqueue(N);
}

void UBH_NotificationSubsystem::PushLevelUp(const FBH_LevelUpInfo& Info)
{
	FBH_Notification N;
	N.Kind = EBH_NotificationKind::LevelUp;
	N.Title = FText::Format(NSLOCTEXT("BlackwoodHollow", "LevelUpTitle", "Level {0}"), FText::AsNumber(Info.NewLevel));
	N.Body = UBH_LevelUpBannerWidget::BuildStatsText(Info);
	Enqueue(N);
}

void UBH_NotificationSubsystem::PushQuest(FText Title, FText Body)
{
	FBH_Notification N;
	N.Kind = EBH_NotificationKind::Quest;
	N.Title = Title;
	N.Body = Body;
	Enqueue(N);
}

void UBH_NotificationSubsystem::PushDiscovery(FText Title, FText Body)
{
	FBH_Notification N;
	N.Kind = EBH_NotificationKind::Discovery;
	N.Title = Title;
	N.Body = Body;
	Enqueue(N);
}

void UBH_NotificationSubsystem::PushDeath(FText Title, FText Body)
{
	FBH_Notification N;
	N.Kind = EBH_NotificationKind::Death;
	N.Title = Title;
	N.Body = Body;
	N.bWarning = true;
	Enqueue(N);
}

void UBH_NotificationSubsystem::ClearAll()
{
	Pending.Reset();
	if (bHasCurrent)
	{
		FinishCurrent();
	}
}

void UBH_NotificationSubsystem::SetSuppressed(bool bInSuppressed)
{
	if (bSuppressed == bInSuppressed)
	{
		return;
	}
	bSuppressed = bInSuppressed;
	if (bSuppressed)
	{
		ClearAll();
	}
}

void UBH_NotificationSubsystem::FinishCurrent()
{
	bHasCurrent = false;
	bEnding = false;
	GapRemaining = UBH_HUDSettings::Get()->NotificationGap;
	OnNotificationFinished.Broadcast();
}

void UBH_NotificationSubsystem::Show(const FBH_Notification& Notification)
{
	const UBH_HUDSettings* S = UBH_HUDSettings::Get();
	Current = Notification;
	bHasCurrent = true;
	bEnding = false;
	CurrentElapsed = 0.f;
	CurrentAge = 0.f;
	CurrentDuration = S->NotificationFadeIn + GetHoldFor(Notification) + S->NotificationFadeOut;
	OnNotificationShown.Broadcast(Current);
}

void UBH_NotificationSubsystem::StartNext()
{
	int32 BestIndex = INDEX_NONE;
	for (int32 Index = 0; Index < Pending.Num(); ++Index)
	{
		const FPending& Entry = Pending[Index];
		if (Entry.ReadyTime > Clock)
		{
			continue;
		}
		if (BestIndex == INDEX_NONE)
		{
			BestIndex = Index;
			continue;
		}
		const FPending& Best = Pending[BestIndex];
		const int32 P = BH_NotificationPriority(Entry.Notification.Kind);
		const int32 BestP = BH_NotificationPriority(Best.Notification.Kind);
		if (P < BestP || (P == BestP && Entry.Sequence < Best.Sequence))
		{
			BestIndex = Index;
		}
	}
	if (BestIndex == INDEX_NONE)
	{
		return;
	}
	const FBH_Notification Next = Pending[BestIndex].Notification;
	Pending.RemoveAt(BestIndex);
	Show(Next);
}

bool UBH_NotificationSubsystem::Tick(float DeltaTime)
{
	Clock += DeltaTime;
	const UBH_HUDSettings* S = UBH_HUDSettings::Get();
	if (bHasCurrent)
	{
		CurrentElapsed += DeltaTime;
		CurrentAge += DeltaTime;
		if (!bEnding && CurrentElapsed >= CurrentDuration - S->NotificationFadeOut)
		{
			bEnding = true;
			OnNotificationEnding.Broadcast();
		}
		if (CurrentElapsed >= CurrentDuration)
		{
			FinishCurrent();
		}
	}
	else if (!Pending.IsEmpty())
	{
		GapRemaining -= DeltaTime;
		if (GapRemaining <= 0.f)
		{
			StartNext();
		}
	}
	return true;
}
