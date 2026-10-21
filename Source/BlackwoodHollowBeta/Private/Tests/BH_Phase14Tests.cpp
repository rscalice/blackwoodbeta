
// Blackwood Hollow - Phase 14-0 automation tests (Fracture eligibility, quest-item drop rule, fragment descriptions, new delegates)
// Run: Session Frontend > Automation > "BlackwoodHollow.Phase14", or headless:
//   UnrealEditor-Cmd.exe <uproject> -ExecCmds="Automation RunTests BlackwoodHollow.Phase14; Quit" -unattended -nullrhi -nosplash
// Pure data-contract tests on static functions, class default objects, reflection and transient objects: no map, no world, no PIE.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/Abilities/AH_GA_FragmentBase.h"
#include "AbilitySystem/Abilities/AH_GA_OverloadBurst.h"
#include "Combat/BH_LoadoutComponent.h"
#include "Items/BH_ArmorItem.h"
#include "Player/BH_PlayerState.h"
#include "Player/BH_RespawnPoint.h"
#include "UI/BH_PlayerMenuLibrary.h"
#include "UObject/UnrealType.h"

#define BH_P14_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// -- #99: Fracture only for players who attuned a Fracture-applying point ---------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_P14_FractureEligibilityTest, "BlackwoodHollow.Phase14.Fracture.Eligibility", BH_P14_FLAGS)
bool FBH_P14_FractureEligibilityTest::RunTest(const FString& Parameters)
{
	using Registry = UBH_RespawnPointSubsystem;

	// Port Vanguard: applies Fracture, not attuned by default.
	TestTrue(TEXT("Attuned Port counts"), Registry::DoesPointMakePlayerFractureEligible(true, false, true));
	TestFalse(TEXT("Un-attuned Port does not count"), Registry::DoesPointMakePlayerFractureEligible(true, false, false));

	// Wreck Camp / basin camp: bAppliesFracture false, attuned by default for everybody.
	TestFalse(TEXT("Starter camp never counts (attuned by default)"), Registry::DoesPointMakePlayerFractureEligible(false, true, true));
	TestFalse(TEXT("A non-fracturing point never counts"), Registry::DoesPointMakePlayerFractureEligible(false, false, true));

	// Defensive: an attuned-by-default point never counts even if someone flags it as fracturing.
	TestFalse(TEXT("Attuned-by-default never counts"), Registry::DoesPointMakePlayerFractureEligible(true, true, true));

	// Placed defaults: the actor class defaults to a fracturing, regular point.
	const ABH_RespawnPoint* PointDefaults = GetDefault<ABH_RespawnPoint>();
	TestTrue(TEXT("Respawn point applies Fracture by default"), PointDefaults->bAppliesFracture);
	TestFalse(TEXT("Respawn point is not attuned by default"), PointDefaults->bAttunedByDefault);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_P14_FractureNullPlayerTest, "BlackwoodHollow.Phase14.Fracture.NullPlayerNotEligible", BH_P14_FLAGS)
bool FBH_P14_FractureNullPlayerTest::RunTest(const FString& Parameters)
{
	// The class default object has no registered points; a null player is never eligible either way.
	const UBH_RespawnPointSubsystem* Registry = GetDefault<UBH_RespawnPointSubsystem>();
	if (!TestNotNull(TEXT("Registry default object"), Registry))
	{
		return false;
	}
	TestFalse(TEXT("Null player state is not eligible"), Registry->HasAttunedFracturePoint(nullptr));
	return true;
}

// -- #123: quest items can never be dropped ----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_P14_QuestItemDropTest, "BlackwoodHollow.Phase14.Menu.QuestItemRefusesDrop", BH_P14_FLAGS)
bool FBH_P14_QuestItemDropTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Item.Quest tag is registered"), UBH_PlayerMenuLibrary::GetQuestItemTag().IsValid());

	UBH_ArmorItem* Item = NewObject<UBH_ArmorItem>(GetTransientPackage());
	if (!TestNotNull(TEXT("Transient item"), Item))
	{
		return false;
	}
	Item->ItemTags.AddTag(UBH_PlayerMenuLibrary::GetQuestItemTag());

	FText Reason;
	TestFalse(TEXT("Item.Quest item refused"), UBH_PlayerMenuLibrary::CanDropItem(Item, Reason));
	TestFalse(TEXT("Refusal carries a reason"), Reason.IsEmpty());

	Item->ItemTags.RemoveTag(UBH_PlayerMenuLibrary::GetQuestItemTag());
	FText OtherReason;
	const bool bDropAllowed = UBH_PlayerMenuLibrary::CanDropItem(Item, OtherReason);
	// Without the tag the refusal (if any) must come from another rule (not removable / equipped), never the quest rule alone.
	TestEqual(TEXT("Allowed exactly when there is no reason"), bDropAllowed, OtherReason.IsEmpty());
	return true;
}

// -- #121: fragment descriptions -----------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_P14_FragmentDescriptionsTest, "BlackwoodHollow.Phase14.Fragments.DefaultDescriptions", BH_P14_FLAGS)
bool FBH_P14_FragmentDescriptionsTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("Overload Burst default description"), UAH_GA_FragmentBase::GetDefaultOverloadBurstDescription().IsEmpty());
	TestFalse(TEXT("Blight Aegis default description"), UAH_GA_FragmentBase::GetDefaultBlightAegisDescription().IsEmpty());
	TestFalse(TEXT("Resonance Pulse default description"), UAH_GA_FragmentBase::GetDefaultResonancePulseDescription().IsEmpty());

	const UAH_GA_OverloadBurst* Burst = GetDefault<UAH_GA_OverloadBurst>();
	TestFalse(TEXT("Overload Burst CDO carries its Description"), Burst->Description.IsEmpty());
	TestEqual(TEXT("Overload Burst CDO uses the default text"), Burst->Description.ToString(), UAH_GA_FragmentBase::GetDefaultOverloadBurstDescription().ToString());
	return true;
}

// -- #119 / #120: the new change delegates exist and are Blueprint-assignable ---------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_P14_DelegatesTest, "BlackwoodHollow.Phase14.Delegates.Exist", BH_P14_FLAGS)
bool FBH_P14_DelegatesTest::RunTest(const FString& Parameters)
{
	const FProperty* EquipmentChanged = UBH_LoadoutComponent::StaticClass()->FindPropertyByName(TEXT("OnEquipmentChanged"));
	if (TestNotNull(TEXT("OnEquipmentChanged on the loadout component"), EquipmentChanged))
	{
		TestTrue(TEXT("OnEquipmentChanged is BlueprintAssignable"), EquipmentChanged->HasAnyPropertyFlags(CPF_BlueprintAssignable));
	}

	const FProperty* BurstLearned = ABH_PlayerState::StaticClass()->FindPropertyByName(TEXT("OnOverloadBurstLearned"));
	if (TestNotNull(TEXT("OnOverloadBurstLearned on the player state"), BurstLearned))
	{
		TestTrue(TEXT("OnOverloadBurstLearned is BlueprintAssignable"), BurstLearned->HasAnyPropertyFlags(CPF_BlueprintAssignable));
	}

	const FProperty* LearnedFlag = ABH_PlayerState::StaticClass()->FindPropertyByName(TEXT("bOverloadBurstLearned"));
	if (TestNotNull(TEXT("bOverloadBurstLearned"), LearnedFlag))
	{
		TestTrue(TEXT("bOverloadBurstLearned replicates with a RepNotify"), LearnedFlag->HasAllPropertyFlags(CPF_Net | CPF_RepNotify));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
