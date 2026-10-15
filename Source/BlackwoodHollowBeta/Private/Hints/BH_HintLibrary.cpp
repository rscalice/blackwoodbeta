// Blackwood Hollow - hint helpers (Phase 13E, implementation)

#include "Hints/BH_HintLibrary.h"
#include "Hints/BH_HintData.h"
#include "Hints/BH_PlayerHintComponent.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/PlatformTime.h"
#include "Modules/ModuleManager.h"
#include "UObject/StrongObjectPtr.h"

DEFINE_LOG_CATEGORY_STATIC(LogBHHints, Log, All);

const TArray<UBH_HintData*>& UBH_HintLibrary::GetAllHints()
{
	static TArray<TStrongObjectPtr<UBH_HintData>> Strong;
	static TArray<UBH_HintData*> Raw;
	static double LastScan = -1000.0;

	bool bStale = Raw.IsEmpty();
	for (const UBH_HintData* Hint : Raw)
	{
		if (!IsValid(Hint))
		{
			bStale = true;
		}
	}
	if (!bStale)
	{
		return Raw;
	}

	// Do not rescan every call while the folder is empty or the registry is still scanning.
	const double Now = FPlatformTime::Seconds();
	if (Now - LastScan < 2.0)
	{
		return Raw;
	}
	LastScan = Now;

	Strong.Reset();
	Raw.Reset();

	const UBH_HintSettings* Settings = UBH_HintSettings::Get();
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	TArray<FAssetData> Assets;
	Registry.GetAssetsByPath(FName(*Settings->HintsFolder), Assets, /*bRecursive*/ true);

	const FTopLevelAssetPath HintClassPath = UBH_HintData::StaticClass()->GetClassPathName();
	for (const FAssetData& Asset : Assets)
	{
		if (Asset.AssetClassPath != HintClassPath)
		{
			continue;
		}
		UBH_HintData* Hint = Cast<UBH_HintData>(Asset.GetAsset());
		if (!Hint || Hint->HintId.IsNone())
		{
			continue;
		}
		Strong.Add(TStrongObjectPtr<UBH_HintData>(Hint));
		Raw.Add(Hint);
	}
	// TArray<T*>::Sort dereferences, so the predicate receives references.
	Raw.Sort([](const UBH_HintData& A, const UBH_HintData& B)
	{
		return A.SortOrder != B.SortOrder ? A.SortOrder < B.SortOrder : A.HintId.LexicalLess(B.HintId);
	});
	UE_LOG(LogBHHints, Log, TEXT("Hint catalogue: %d hint(s) in %s."), Raw.Num(), *Settings->HintsFolder);
	return Raw;
}

UBH_HintData* UBH_HintLibrary::FindHint(FName HintId)
{
	for (UBH_HintData* Hint : GetAllHints())
	{
		if (Hint && Hint->HintId == HintId)
		{
			return Hint;
		}
	}
	return nullptr;
}

UBH_PlayerHintComponent* UBH_HintLibrary::FindPlayerHintComponent(const UObject* Context)
{
	const APlayerState* PS = Cast<APlayerState>(Context);
	if (!PS)
	{
		if (const APawn* Pawn = Cast<APawn>(Context))
		{
			PS = Pawn->GetPlayerState();
		}
		else if (const APlayerController* PC = Cast<APlayerController>(Context))
		{
			PS = PC->PlayerState;
		}
	}
	return PS ? PS->FindComponentByClass<UBH_PlayerHintComponent>() : nullptr;
}

void UBH_HintLibrary::TriggerHintKey(const UObject* Context, FName Key)
{
	if (Key.IsNone())
	{
		return;
	}
	if (UBH_PlayerHintComponent* Component = FindPlayerHintComponent(Context))
	{
		Component->FireKey(Key);
	}
}

void UBH_HintLibrary::TriggerHint(const UObject* Context, FName HintId)
{
	if (UBH_PlayerHintComponent* Component = FindPlayerHintComponent(Context))
	{
		Component->FireHint(HintId);
	}
}
