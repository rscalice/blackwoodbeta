// Blackwood Hollow - Phase 13I-1 automation tests (partner panel data)
// Run: Session Frontend > Automation > "BlackwoodHollow.Phase13I", or headless:
//   UnrealEditor-Cmd.exe <uproject> -ExecCmds="Automation RunTests BlackwoodHollow.Phase13I; Quit" -unattended -nullrhi -nosplash
// Pure mapping tests (no world): blight stacks, ping quality thresholds, struct defaults.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Player/BH_PartyLibrary.h"
#include "Player/BH_PartyStatus.h"

#define BH_P13I_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_P13I_BlightStacksTest, "BlackwoodHollow.Phase13I.PartyStatus.BlightStacks", BH_P13I_FLAGS)
bool FBH_P13I_BlightStacksTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("0 -> 0"), UBH_PartyLibrary::BlightBuildupToStacks(0.f), 0);
	TestEqual(TEXT("9.9 -> 0 (rounds down)"), UBH_PartyLibrary::BlightBuildupToStacks(9.9f), 0);
	TestEqual(TEXT("10 -> 1"), UBH_PartyLibrary::BlightBuildupToStacks(10.f), 1);
	TestEqual(TEXT("55 -> 5"), UBH_PartyLibrary::BlightBuildupToStacks(55.f), 5);
	TestEqual(TEXT("99.9 -> 9"), UBH_PartyLibrary::BlightBuildupToStacks(99.9f), 9);
	TestEqual(TEXT("100 -> 10"), UBH_PartyLibrary::BlightBuildupToStacks(100.f), 10);
	TestEqual(TEXT("250 clamps to 10"), UBH_PartyLibrary::BlightBuildupToStacks(250.f), 10);
	TestEqual(TEXT("-5 clamps to 0"), UBH_PartyLibrary::BlightBuildupToStacks(-5.f), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_P13I_PingQualityTest, "BlackwoodHollow.Phase13I.PartyStatus.PingThresholds", BH_P13I_FLAGS)
bool FBH_P13I_PingQualityTest::RunTest(const FString& Parameters)
{
	const auto Classify = [](float PingMs)
	{
		return static_cast<int32>(UBH_PartyLibrary::ClassifyPing(PingMs, 80.f, 150.f));
	};
	TestEqual(TEXT("0 ms good"), Classify(0.f), static_cast<int32>(EBH_PingQuality::Good));
	TestEqual(TEXT("79 ms good"), Classify(79.f), static_cast<int32>(EBH_PingQuality::Good));
	TestEqual(TEXT("80 ms fair"), Classify(80.f), static_cast<int32>(EBH_PingQuality::Fair));
	TestEqual(TEXT("149 ms fair"), Classify(149.f), static_cast<int32>(EBH_PingQuality::Fair));
	TestEqual(TEXT("150 ms poor"), Classify(150.f), static_cast<int32>(EBH_PingQuality::Poor));
	TestEqual(TEXT("999 ms poor"), Classify(999.f), static_cast<int32>(EBH_PingQuality::Poor));

	// Config defaults are the spec's thresholds.
	const UBH_PartyLibrary* Defaults = GetDefault<UBH_PartyLibrary>();
	TestEqual(TEXT("good threshold default"), Defaults->PingGoodBelowMs, 80.f);
	TestEqual(TEXT("fair threshold default"), Defaults->PingFairBelowMs, 150.f);
	TestEqual(TEXT("disconnected display default"), Defaults->DisconnectedDisplaySeconds, 3.f);
	TestEqual(TEXT("null player state is good"), static_cast<int32>(UBH_PartyLibrary::GetPingQuality(nullptr)), static_cast<int32>(EBH_PingQuality::Good));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_P13I_StatusStructTest, "BlackwoodHollow.Phase13I.PartyStatus.StructDefaults", BH_P13I_FLAGS)
bool FBH_P13I_StatusStructTest::RunTest(const FString& Parameters)
{
	const FBH_PartyMemberStatus Fresh;
	TestEqual(TEXT("full health"), Fresh.HealthFraction, 1.f);
	TestEqual(TEXT("no blight"), Fresh.BlightStacks, 0);
	TestEqual(TEXT("alive"), static_cast<int32>(Fresh.DeathState), static_cast<int32>(EBH_DeathPhase::Alive));
	TestFalse(TEXT("not in combat"), Fresh.bInCombat);
	TestFalse(TEXT("not fractured"), Fresh.bFractured);
	TestEqual(TEXT("level 1"), Fresh.Level, 1);

	FBH_PartyMemberStatus Changed = Fresh;
	TestTrue(TEXT("copy is equal"), Changed == Fresh);
	Changed.BlightStacks = 3;
	TestTrue(TEXT("a field change is detected"), Changed != Fresh);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
