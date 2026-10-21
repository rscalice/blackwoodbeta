
// Blackwood Hollow - Phase 14A-1 quest director automation tests (event vocabulary, objective formatting, director defaults)
// Run: Session Frontend > Automation > "BlackwoodHollow.Phase14.Quest", or headless:
//   UnrealEditor-Cmd.exe <uproject> -ExecCmds="Automation RunTests BlackwoodHollow.Phase14.Quest; Quit" -unattended -nullrhi -nosplash
// Pure data-contract tests on static helpers and class default objects: no map, no world, no PIE.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Quests/BH_QuestDirector.h"
#include "Quests/BH_QuestEvents.h"
#include "Quests/BH_QuestTags.h"
#include "Quests/BH_QuestTypes.h"

#define BH_P14Q_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_P14Q_VocabularyTest, "BlackwoodHollow.Phase14.Quest.Vocabulary", BH_P14Q_FLAGS)
bool FBH_P14Q_VocabularyTest::RunTest(const FString& Parameters)
{
	const TArray<FString> Names = {
		BH_QuestEvents::OpenContainer, BH_QuestEvents::TakeItem, BH_QuestEvents::WorldFlag, BH_QuestEvents::Kill,
		BH_QuestEvents::TalkTo, BH_QuestEvents::EnterArea, BH_QuestEvents::LearnSkill, BH_QuestEvents::Relight,
		BH_QuestEvents::Attune, BH_QuestEvents::Interact
	};
	TSet<FString> Unique;
	for (const FString& Name : Names)
	{
		TestFalse(TEXT("Event name is not empty"), Name.IsEmpty());
		Unique.Add(Name);
	}
	TestEqual(TEXT("Event names are distinct"), Unique.Num(), Names.Num());
	TestFalse(TEXT("Fixed skill argument is not empty"), FString(BH_QuestEvents::SkillOverloadBurst).IsEmpty());

	TestEqual(TEXT("ClassShortName strips _C"), BH_QuestEvents::ClassShortName(TEXT("BPI_Weapon_Longsword_C")), FString(TEXT("BPI_Weapon_Longsword")));
	TestEqual(TEXT("ClassShortName keeps plain names"), BH_QuestEvents::ClassShortName(TEXT("BPI_Weapon_Longsword")), FString(TEXT("BPI_Weapon_Longsword")));
	TestEqual(TEXT("Ranked kill argument"), BH_QuestEvents::MakeRankedKillArgument(TEXT("CorruptedCrab"), TEXT("Veteran")), FString(TEXT("CorruptedCrab.Veteran")));
	TestEqual(TEXT("NormalizeArgument trims"), BH_QuestEvents::NormalizeArgument(TEXT("  Wreck.Crate1 ")), FString(TEXT("Wreck.Crate1")));
	TestEqual(TEXT("Describe"), BH_QuestEvents::Describe(TEXT("OpenContainer"), TEXT("Wreck.Crate1")), FString(TEXT("OpenContainer(Wreck.Crate1)")));
	// Narrative's rule: lower case, no spaces, "_" between task and argument.
	TestEqual(TEXT("MakeTaskString"), BH_QuestEvents::MakeTaskString(TEXT("OpenContainer"), TEXT("Wreck Crate1")), FString(TEXT("opencontainer_wreckcrate1")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_P14Q_ObjectiveFormatTest, "BlackwoodHollow.Phase14.Quest.ObjectiveFormat", BH_P14Q_FLAGS)
bool FBH_P14Q_ObjectiveFormatTest::RunTest(const FString& Parameters)
{
	const FText Desc = FText::FromString(TEXT("Open the crates"));
	TestEqual(TEXT("Count shown when more than one needed"), UBH_QuestDirector::FormatObjectiveLine(Desc, 1, 3).ToString(), FString(TEXT("Open the crates (1/3)")));
	TestEqual(TEXT("No count for a single task"), UBH_QuestDirector::FormatObjectiveLine(Desc, 0, 1).ToString(), FString(TEXT("Open the crates")));

	TArray<FBH_QuestObjectiveLine> Lines;
	for (int32 Index = 0; Index < 5; ++Index)
	{
		FBH_QuestObjectiveLine Line;
		Line.Text = FText::FromString(FString::Printf(TEXT("Line%d"), Index));
		Lines.Add(Line);
	}
	const FString Capped = UBH_QuestDirector::BuildObjectiveText(Lines, 3).ToString();
	TestTrue(TEXT("Third line included"), Capped.Contains(TEXT("Line2")));
	TestFalse(TEXT("Fourth line cut off by MaxLines"), Capped.Contains(TEXT("Line3")));
	TestTrue(TEXT("No lines gives empty text"), UBH_QuestDirector::BuildObjectiveText(TArray<FBH_QuestObjectiveLine>(), 3).IsEmpty());

	FBH_QuestObjective A;
	FBH_QuestObjective B;
	TestTrue(TEXT("Empty objectives are equal"), UBH_QuestDirector::AreObjectivesEqual(A, B));
	B.ObjectiveText = FText::FromString(TEXT("Changed"));
	TestFalse(TEXT("Different text is not equal"), UBH_QuestDirector::AreObjectivesEqual(A, B));

	FBH_QuestObjective Objective;
	FBH_QuestObjectiveLine Done;
	Done.bComplete = true;
	FBH_QuestObjectiveLine Open;
	Open.TargetTag = TAG_Quest_Target.GetTag();
	Objective.Lines = { Done, Open };
	TestEqual(TEXT("First open line skips completed lines"), Objective.FindFirstOpenLine(), 1);
	TestNotNull(TEXT("Line found for its target tag"), Objective.FindLineForTarget(TAG_Quest_Target.GetTag()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_P14Q_DirectorDefaultsTest, "BlackwoodHollow.Phase14.Quest.DirectorDefaults", BH_P14Q_FLAGS)
bool FBH_P14Q_DirectorDefaultsTest::RunTest(const FString& Parameters)
{
	const UBH_QuestDirector* Defaults = GetDefault<UBH_QuestDirector>();
	TestEqual(TEXT("Three objective lines"), Defaults->MaxObjectiveLines, 3);
	TestFalse(TEXT("Events without a quest are not recorded"), Defaults->bRecordEventsWithoutQuest);
	TestTrue(TEXT("Event history is capped"), Defaults->MaxRecordedEvents > 0);
	TestTrue(TEXT("Target table path"), Defaults->QuestTargetTable.ToSoftObjectPath().ToString().EndsWith(TEXT("DT_BH_QuestTargets")));
	TestNull(TEXT("No main quest by default"), Defaults->GetMainQuestClass().Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_P14Q_ChainDefaultsTest, "BlackwoodHollow.Phase14.Quest.ChainDefaults", BH_P14Q_FLAGS)
bool FBH_P14Q_ChainDefaultsTest::RunTest(const FString& Parameters)
{
	const UBH_QuestDirector* Defaults = GetDefault<UBH_QuestDirector>();
	TestTrue(TEXT("Chain auto-starts"), Defaults->bAutoStartQuestChain);
	TestEqual(TEXT("One auto-start level"), Defaults->AutoStartLevels.Num(), 1);
	TestTrue(TEXT("Auto-start level is L_WreckageShallows"), Defaults->AutoStartLevels.Contains(FName(TEXT("L_WreckageShallows"))));

	const TArray<FBH_QuestChainEntry>& Chain = Defaults->QuestChain;
	if (!TestEqual(TEXT("Three chain entries"), Chain.Num(), 3))
	{
		return false;
	}
	const TCHAR* Paths[] = {
		TEXT("/Game/BlackwoodHollow/Quests/Q_AmongTheWrack.Q_AmongTheWrack_C"),
		TEXT("/Game/BlackwoodHollow/Quests/Q_KingsRoad.Q_KingsRoad_C"),
		TEXT("/Game/BlackwoodHollow/Quests/Q_GuardiansOfTheSpan.Q_GuardiansOfTheSpan_C") };
	const int32 XP[] = { 60, 150, 100 };
	for (int32 Index = 0; Index < 3; ++Index)
	{
		TestEqual(*FString::Printf(TEXT("Entry %d path"), Index), Chain[Index].QuestClass.ToSoftObjectPath().ToString(), FString(Paths[Index]));
		TestEqual(*FString::Printf(TEXT("Entry %d XP"), Index), Chain[Index].XPReward, XP[Index]);
		TestTrue(*FString::Printf(TEXT("Entry %d is a main quest"), Index), Chain[Index].bMain);
	}
	TestEqual(TEXT("FindChainIndex finds the second entry"), UBH_QuestDirector::FindChainIndex(Chain, FSoftObjectPath(Paths[1])), 1);
	TestEqual(TEXT("FindChainIndex misses an unknown path"), UBH_QuestDirector::FindChainIndex(Chain, FSoftObjectPath(TEXT("/Game/X.X_C"))), static_cast<int32>(INDEX_NONE));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_P14Q_ChainSelectionTest, "BlackwoodHollow.Phase14.Quest.ChainSelection", BH_P14Q_FLAGS)
bool FBH_P14Q_ChainSelectionTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Nothing handled: first entry"), UBH_QuestDirector::SelectNextChainIndex({ false, false, false }, -1), 0);
	TestEqual(TEXT("First handled: second entry"), UBH_QuestDirector::SelectNextChainIndex({ true, false, false }, -1), 1);
	TestEqual(TEXT("After entry 0: second entry"), UBH_QuestDirector::SelectNextChainIndex({ true, false, false }, 0), 1);
	TestEqual(TEXT("Skips handled entries after the index"), UBH_QuestDirector::SelectNextChainIndex({ true, true, false }, 0), 2);
	TestEqual(TEXT("Never goes back before the index"), UBH_QuestDirector::SelectNextChainIndex({ false, true, true }, 1), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("All handled: none"), UBH_QuestDirector::SelectNextChainIndex({ true, true, true }, -1), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("Empty chain: none"), UBH_QuestDirector::SelectNextChainIndex({}, -1), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("Index past the end: none"), UBH_QuestDirector::SelectNextChainIndex({ false }, 5), static_cast<int32>(INDEX_NONE));

	const TArray<FName> Levels = { FName(TEXT("L_WreckageShallows")) };
	TestTrue(TEXT("Plain map name"), UBH_QuestDirector::IsAutoStartLevel(Levels, TEXT("L_WreckageShallows")));
	TestTrue(TEXT("PIE-prefixed map name"), UBH_QuestDirector::IsAutoStartLevel(Levels, TEXT("UEDPIE_0_L_WreckageShallows")));
	TestTrue(TEXT("Package path with PIE prefix"), UBH_QuestDirector::IsAutoStartLevel(Levels, TEXT("/Game/Maps/UEDPIE_1_L_WreckageShallows")));
	TestFalse(TEXT("Other level"), UBH_QuestDirector::IsAutoStartLevel(Levels, TEXT("UEDPIE_0_L_Other")));
	TestFalse(TEXT("Empty name"), UBH_QuestDirector::IsAutoStartLevel(Levels, FString()));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
