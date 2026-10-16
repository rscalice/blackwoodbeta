// Blackwood Hollow - Phase 13 automation tests (13C notification priority, 13E hints, perfect block, 13B+ beats)
// Run: Session Frontend > Automation > "BlackwoodHollow.Phase13", or headless:
//   UnrealEditor-Cmd.exe <uproject> -ExecCmds="Automation RunTests BlackwoodHollow.Phase13; Quit" -unattended -nullrhi -nosplash
// These are data-contract and default-value tests: they need no map and no PIE, only the editor's asset registry.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "UI/BH_NotificationTypes.h"
#include "Hints/BH_HintData.h"
#include "Hints/BH_HintLibrary.h"
#include "AbilitySystem/Abilities/AH_GA_Block.h"
#include "Beats/BH_BeatTypes.h"
#include "Engine/DataTable.h"
#include "InputAction.h"

#define BH_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace BHPhase13Tests
{
	static const TCHAR* BeatsTablePath = TEXT("/Game/BlackwoodHollow/Data/DT_BH_Beats.DT_BH_Beats");
	static const TCHAR* BlockAbilityPath = TEXT("/Game/BlackwoodHollow/Combat/Abilities/GA_SnS_Block.GA_SnS_Block_C");
	static const int32 ExpectedHintCount = 18;
}

// 13C: notification priority. Death > LevelUp > Quest > Hint > Discovery > Loot (Notice ranks with Loot) > Experience.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_NotificationPriorityTest, "BlackwoodHollow.Phase13.Notifications.Priority", BH_TEST_FLAGS)
bool FBH_NotificationPriorityTest::RunTest(const FString& Parameters)
{
	using K = EBH_NotificationKind;
	const int32 Death = BH_NotificationPriority(K::Death);
	const int32 LevelUp = BH_NotificationPriority(K::LevelUp);
	const int32 Quest = BH_NotificationPriority(K::Quest);
	const int32 Hint = BH_NotificationPriority(K::Hint);
	const int32 Discovery = BH_NotificationPriority(K::Discovery);
	const int32 Loot = BH_NotificationPriority(K::Loot);
	const int32 Notice = BH_NotificationPriority(K::Notice);
	const int32 Xp = BH_NotificationPriority(K::Experience);

	TestTrue(TEXT("Death before LevelUp"), Death < LevelUp);
	TestTrue(TEXT("LevelUp before Quest"), LevelUp < Quest);
	TestTrue(TEXT("Quest before Hint"), Quest < Hint);
	TestTrue(TEXT("Hint before Discovery"), Hint < Discovery);
	TestTrue(TEXT("Discovery before Loot"), Discovery < Loot);
	TestEqual(TEXT("Notice ranks with Loot"), Notice, Loot);
	TestTrue(TEXT("Loot before Experience"), Loot < Xp);
	return true;
}

// 13E: the documented hint settings values (balance.md section 10).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_HintSettingsTest, "BlackwoodHollow.Phase13.Hints.Settings", BH_TEST_FLAGS)
bool FBH_HintSettingsTest::RunTest(const FString& Parameters)
{
	const UBH_HintSettings* S = UBH_HintSettings::Get();
	if (!TestNotNull(TEXT("Hint settings exist"), S)) { return false; }

	TestTrue(TEXT("Hints enabled"), S->bHintsEnabled);
	TestEqual(TEXT("HintsFolder"), S->HintsFolder, FString(TEXT("/Game/BlackwoodHollow/Data/Hints")));
	TestEqual(TEXT("SaveSlotPrefix"), S->SaveSlotPrefix, FString(TEXT("BH_SeenHints_")));
	TestEqual(TEXT("DefaultHoldSeconds"), S->DefaultHoldSeconds, 7.f);
	TestEqual(TEXT("MinGapSeconds"), S->MinGapSeconds, 4.f);
	TestEqual(TEXT("BossIntroSeconds"), S->BossIntroSeconds, 4.f);
	TestEqual(TEXT("BannerFadeInSeconds"), S->BannerFadeInSeconds, 0.3f);
	TestEqual(TEXT("BannerFadeOutSeconds"), S->BannerFadeOutSeconds, 0.5f);
	TestEqual(TEXT("TrackerSwapSeconds"), S->TrackerSwapSeconds, 0.2f);
	TestEqual(TEXT("BannerRisePixels"), S->BannerRisePixels, 8.f);
	TestEqual(TEXT("ServerPollInterval"), S->ServerPollInterval, 0.2f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_HintSaveGameTest, "BlackwoodHollow.Phase13.Hints.SaveGameStartsEmpty", BH_TEST_FLAGS)
bool FBH_HintSaveGameTest::RunTest(const FString& Parameters)
{
	const UBH_HintSaveGame* Save = GetDefault<UBH_HintSaveGame>();
	if (!TestNotNull(TEXT("SaveGame CDO"), Save)) { return false; }
	TestEqual(TEXT("No hints seen on a fresh profile"), Save->SeenHints.Num(), 0);
	return true;
}

// 13E: the hint catalogue (the 18 Data Assets under /Game/BlackwoodHollow/Data/Hints).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_HintCatalogueTest, "BlackwoodHollow.Phase13.Hints.Catalogue", BH_TEST_FLAGS)
bool FBH_HintCatalogueTest::RunTest(const FString& Parameters)
{
	const TArray<UBH_HintData*>& Hints = UBH_HintLibrary::GetAllHints();
	TestEqual(TEXT("Hint asset count"), Hints.Num(), BHPhase13Tests::ExpectedHintCount);

	TSet<FName> Ids;
	for (const UBH_HintData* H : Hints)
	{
		if (!TestNotNull(TEXT("Hint asset is valid"), H)) { continue; }
		const FString Name = H->GetName();

		TestFalse(*FString::Printf(TEXT("%s: HintId set"), *Name), H->HintId.IsNone());
		TestFalse(*FString::Printf(TEXT("%s: unique id %s"), *Name, *H->HintId.ToString()), Ids.Contains(H->HintId));
		Ids.Add(H->HintId);

		TestFalse(*FString::Printf(TEXT("%s: Title set"), *Name), H->Title.IsEmpty());
		TestFalse(*FString::Printf(TEXT("%s: Text set"), *Name), H->Text.IsEmpty());
		TestTrue(*FString::Printf(TEXT("%s: HoldSeconds >= 0"), *Name), H->HoldSeconds >= 0.f);
		TestTrue(*FString::Printf(TEXT("%s: DelaySeconds >= 0"), *Name), H->DelaySeconds >= 0.f);
		TestTrue(*FString::Printf(TEXT("%s: at most 3 glyph actions"), *Name), H->GlyphActions.Num() <= 3);

		for (const UInputAction* IA : H->GlyphActions)
		{
			// A deleted input action (IA_Parry was removed on 2026-10-15) leaves a null entry.
			TestNotNull(*FString::Printf(TEXT("%s: glyph action is not null"), *Name), IA);
		}
	}

	// RequiresSeen must reference existing hints, and the graph must be acyclic.
	TMap<FName, TArray<FName>> Requires;
	for (const UBH_HintData* H : Hints)
	{
		if (!H) { continue; }
		Requires.Add(H->HintId, H->RequiresSeen);
		for (const FName& R : H->RequiresSeen)
		{
			TestTrue(*FString::Printf(TEXT("%s: RequiresSeen '%s' exists"), *H->GetName(), *R.ToString()), Ids.Contains(R));
			TestTrue(*FString::Printf(TEXT("%s: does not require itself"), *H->GetName()), R != H->HintId);
		}
	}
	for (const TPair<FName, TArray<FName>>& Pair : Requires)
	{
		TSet<FName> Visited;
		TArray<FName> Stack = Pair.Value;
		bool bCycle = false;
		while (Stack.Num() > 0)
		{
			const FName Cur = Stack.Pop();
			if (Cur == Pair.Key) { bCycle = true; break; }
			if (Visited.Contains(Cur)) { continue; }
			Visited.Add(Cur);
			if (const TArray<FName>* Next = Requires.Find(Cur)) { Stack.Append(*Next); }
		}
		TestFalse(*FString::Printf(TEXT("%s: RequiresSeen has no cycle"), *Pair.Key.ToString()), bCycle);
	}

	// Every id the plan lists must exist.
	static const TCHAR* Expected[] = {
		TEXT("MoveLook"), TEXT("Interact"), TEXT("DrawSword"), TEXT("Attack"), TEXT("Dodge"), TEXT("LockOn"), TEXT("Block"),
		TEXT("Parry"), TEXT("PostureBreak"), TEXT("Heal"), TEXT("Blight"), TEXT("Harvest"), TEXT("Incense"),
		TEXT("OverloadBurst"), TEXT("EnemyRanks"), TEXT("KiteShield"), TEXT("Fracture"), TEXT("Revive") };
	for (const TCHAR* Id : Expected)
	{
		TestTrue(*FString::Printf(TEXT("Hint '%s' exists"), Id), Ids.Contains(FName(Id)));
		TestNotNull(*FString::Printf(TEXT("FindHint('%s')"), Id), UBH_HintLibrary::FindHint(FName(Id)));
	}
	TestNull(TEXT("FindHint of an unknown id is null"), UBH_HintLibrary::FindHint(FName(TEXT("NoSuchHint"))));
	return true;
}

// Perfect block: the hint that teaches it (id stays "Parry" so saved seen lists keep working).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_PerfectBlockHintTest, "BlackwoodHollow.Phase13.PerfectBlock.Hint", BH_TEST_FLAGS)
bool FBH_PerfectBlockHintTest::RunTest(const FString& Parameters)
{
	const UBH_HintData* H = UBH_HintLibrary::FindHint(FName(TEXT("Parry")));
	if (!TestNotNull(TEXT("Hint with id 'Parry' exists"), H)) { return false; }

	TestTrue(TEXT("Title says Perfect block"), H->Title.ToString().Contains(TEXT("Perfect block"), ESearchCase::IgnoreCase));
	TestEqual(TEXT("Trigger is Manual"), static_cast<uint8>(H->Trigger), static_cast<uint8>(EBH_HintTrigger::Manual));
	TestEqual(TEXT("TriggerKey"), H->TriggerKey, FName(TEXT("Combat.PerfectBlock")));
	TestTrue(TEXT("Waits for the Block hint"), H->RequiresSeen.Contains(FName(TEXT("Block"))));

	if (TestTrue(TEXT("Has a glyph action"), H->GlyphActions.Num() > 0) && TestNotNull(TEXT("Glyph action valid"), H->GlyphActions[0].Get()))
	{
		TestEqual(TEXT("Glyph is IA_Block (there is no parry key)"), H->GlyphActions[0]->GetName(), FString(TEXT("IA_Block")));
	}

	const UBH_HintData* Block = UBH_HintLibrary::FindHint(FName(TEXT("Block")));
	if (TestNotNull(TEXT("Block hint exists"), Block))
	{
		TestFalse(TEXT("Block hint does not itself wait for Parry"), Block->RequiresSeen.Contains(FName(TEXT("Parry"))));
	}
	return true;
}

// Perfect block: native defaults, and the Sword block Blueprint's tuned values.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_BlockDefaultsTest, "BlackwoodHollow.Phase13.PerfectBlock.NativeDefaults", BH_TEST_FLAGS)
bool FBH_BlockDefaultsTest::RunTest(const FString& Parameters)
{
	const UAH_GA_Block* Block = GetDefault<UAH_GA_Block>();
	if (!TestNotNull(TEXT("UAH_GA_Block CDO"), Block)) { return false; }

	TestEqual(TEXT("ParryWindow starts at 0.2 s"), Block->ParryWindow, 0.2f);
	TestEqual(TEXT("DamageReduction"), Block->DamageReduction, 0.8f);
	TestEqual(TEXT("BlockAngleDegrees"), Block->BlockAngleDegrees, 120.f);
	TestEqual(TEXT("SwordBlockStaminaScale"), Block->SwordBlockStaminaScale, 0.9f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_BlockBlueprintTest, "BlackwoodHollow.Phase13.PerfectBlock.SwordBlockBlueprint", BH_TEST_FLAGS)
bool FBH_BlockBlueprintTest::RunTest(const FString& Parameters)
{
	UClass* Cls = LoadClass<UAH_GA_Block>(nullptr, BHPhase13Tests::BlockAbilityPath);
	if (!TestNotNull(TEXT("GA_SnS_Block loads"), Cls)) { return false; }

	const UAH_GA_Block* CDO = Cls->GetDefaultObject<UAH_GA_Block>();
	if (!TestNotNull(TEXT("GA_SnS_Block CDO"), CDO)) { return false; }

	// A usable human-scale window: not zero (perfect block impossible), not so wide it trivialises blocking.
	TestTrue(TEXT("ParryWindow > 0.05 s"), CDO->ParryWindow > 0.05f);
	TestTrue(TEXT("ParryWindow <= 0.5 s"), CDO->ParryWindow <= 0.5f);
	TestEqual(TEXT("Sword block arc stays 120 degrees"), CDO->BlockAngleDegrees, 120.f);
	TestEqual(TEXT("Sword block DamageReduction stays 0.8"), CDO->DamageReduction, 0.8f);
	return true;
}

// 13B+: the beat table (nine rows, unique ids, text set).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_BeatTableTest, "BlackwoodHollow.Phase13.Beats.Table", BH_TEST_FLAGS)
bool FBH_BeatTableTest::RunTest(const FString& Parameters)
{
	UDataTable* Table = LoadObject<UDataTable>(nullptr, BHPhase13Tests::BeatsTablePath);
	if (!TestNotNull(TEXT("DT_BH_Beats loads"), Table)) { return false; }

	TestTrue(TEXT("Row struct is FBH_BeatRow"), Table->GetRowStruct() == FBH_BeatRow::StaticStruct());

	TArray<FBH_BeatRow*> Rows;
	Table->GetAllRows<FBH_BeatRow>(TEXT("BHPhase13Tests"), Rows);
	TestEqual(TEXT("Nine beats on Island 1"), Rows.Num(), 9);

	TSet<FGameplayTag> Seen;
	for (const FBH_BeatRow* Row : Rows)
	{
		if (!TestNotNull(TEXT("Row valid"), Row)) { continue; }
		TestTrue(TEXT("BeatId is valid"), Row->BeatId.IsValid());
		TestTrue(TEXT("BeatId under Beat.Island1"), Row->BeatId.ToString().StartsWith(TEXT("Beat.Island1.")));
		TestFalse(*FString::Printf(TEXT("Unique BeatId %s"), *Row->BeatId.ToString()), Seen.Contains(Row->BeatId));
		Seen.Add(Row->BeatId);
		TestFalse(*FString::Printf(TEXT("%s: ObjectiveText set"), *Row->BeatId.ToString()), Row->ObjectiveText.IsEmpty());
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
