
// Blackwood Hollow - Phase 13G-0 automation tests (Player Menu helpers, equip/drop RPCs, first-open cue, character preview)
// Run: Session Frontend > Automation > "BlackwoodHollow.Phase13G", or headless:
//   UnrealEditor-Cmd.exe <uproject> -ExecCmds="Automation RunTests BlackwoodHollow.Phase13G; Quit" -unattended -nullrhi -nosplash
// Pure data-contract tests on plain structs, class default objects and transient objects: no map, no world, no PIE.

#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Items/BH_ArmorItem.h"
#include "Items/BH_EquipmentTypes.h"
#include "Player/BH_PlayerMenuComponent.h"
#include "UI/BH_CharacterPreview.h"
#include "UI/BH_MenuActivatableWidget.h"
#include "UI/BH_PlayerMenuLibrary.h"
#include "UI/BH_UILayoutSubsystem.h"
#include "Components/SceneCaptureComponent2D.h"
#include "UObject/UnrealType.h"

#define BH_P13G_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace BHPhase13GTests
{
	static bool IsReliableServerRpc(const UClass* Class, const FName FunctionName)
	{
		const UFunction* Func = Class ? Class->FindFunctionByName(FunctionName) : nullptr;
		return Func
			&& Func->HasAnyFunctionFlags(FUNC_Net)
			&& Func->HasAnyFunctionFlags(FUNC_NetServer)
			&& Func->HasAnyFunctionFlags(FUNC_NetReliable);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_P13G_SlotNamesTest, "BlackwoodHollow.Phase13G.Library.SlotNames", BH_P13G_FLAGS)
bool FBH_P13G_SlotNamesTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Head = Helm"), UBH_PlayerMenuLibrary::GetEquipSlotDisplayName(EBH_EquipSlot::Head).ToString(), FString(TEXT("Helm")));
	TestEqual(TEXT("Arms = Gloves"), UBH_PlayerMenuLibrary::GetEquipSlotDisplayName(EBH_EquipSlot::Arms).ToString(), FString(TEXT("Gloves")));
	TestEqual(TEXT("Legs = Pants"), UBH_PlayerMenuLibrary::GetEquipSlotDisplayName(EBH_EquipSlot::Legs).ToString(), FString(TEXT("Pants")));
	TestEqual(TEXT("Feet = Boots"), UBH_PlayerMenuLibrary::GetEquipSlotDisplayName(EBH_EquipSlot::Feet).ToString(), FString(TEXT("Boots")));
	TestEqual(TEXT("Main B"), UBH_PlayerMenuLibrary::GetEquipSlotDisplayName(EBH_EquipSlot::Weapon_Main_B).ToString(), FString(TEXT("Main Hand B")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_P13G_DeltaMathTest, "BlackwoodHollow.Phase13G.Library.StatDeltas", BH_P13G_FLAGS)
bool FBH_P13G_DeltaMathTest::RunTest(const FString& Parameters)
{
	FBH_EquipStatBonuses Current;
	Current.AttackPower = 20.f;
	Current.Defense = 10.f;
	Current.MaxStamina = 100.f;
	FBH_EquipStatBonuses Replaced;
	Replaced.Defense = 4.f;
	FBH_EquipStatBonuses Incoming;
	Incoming.AttackPower = 5.f;
	Incoming.Defense = 9.f;

	TArray<FBH_ItemStatDelta> Rows;
	UBH_PlayerMenuLibrary::BuildStatDeltaRows(Current, Replaced, Incoming, Rows);
	if (!TestEqual(TEXT("Three rows"), Rows.Num(), 3))
	{
		return false;
	}
	for (const FBH_ItemStatDelta& Row : Rows)
	{
		if (Row.StatId == TEXT("AttackPower"))
		{
			TestEqual(TEXT("Attack after"), Row.AfterEquip, 25.f);
			TestEqual(TEXT("Attack delta"), Row.Delta, 5.f);
		}
		else if (Row.StatId == TEXT("Defense"))
		{
			TestEqual(TEXT("Defense after"), Row.AfterEquip, 15.f);
			TestEqual(TEXT("Defense delta"), Row.Delta, 5.f);
		}
		else if (Row.StatId == TEXT("MaxStamina"))
		{
			TestEqual(TEXT("Stamina unchanged"), Row.Delta, 0.f);
		}
		else
		{
			AddError(FString::Printf(TEXT("Unexpected stat id %s"), *Row.StatId.ToString()));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_P13G_SetBLockTest, "BlackwoodHollow.Phase13G.Library.SetBLocked", BH_P13G_FLAGS)
bool FBH_P13G_SetBLockTest::RunTest(const FString& Parameters)
{
	FText Reason;
	TestFalse(TEXT("Main B refused while locked"), UBH_PlayerMenuLibrary::IsEquipSlotAllowed(false, EBH_EquipSlot::Weapon_Main_B, Reason));
	TestFalse(TEXT("Reason given"), Reason.IsEmpty());
	TestFalse(TEXT("Off B refused while locked"), UBH_PlayerMenuLibrary::IsEquipSlotAllowed(false, EBH_EquipSlot::Weapon_Off_B, Reason));
	TestTrue(TEXT("Main B allowed once unlocked"), UBH_PlayerMenuLibrary::IsEquipSlotAllowed(true, EBH_EquipSlot::Weapon_Main_B, Reason));
	TestTrue(TEXT("Main A always allowed"), UBH_PlayerMenuLibrary::IsEquipSlotAllowed(false, EBH_EquipSlot::Weapon_Main_A, Reason));
	TestTrue(TEXT("Armor always allowed"), UBH_PlayerMenuLibrary::IsEquipSlotAllowed(false, EBH_EquipSlot::Chest, Reason));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_P13G_DropRulesTest, "BlackwoodHollow.Phase13G.Library.DropRules", BH_P13G_FLAGS)
bool FBH_P13G_DropRulesTest::RunTest(const FString& Parameters)
{
	FText Reason;
	TestFalse(TEXT("Null item refused"), UBH_PlayerMenuLibrary::CanDropItem(nullptr, Reason));

	TestTrue(TEXT("Quest tag is valid"), UBH_PlayerMenuLibrary::GetQuestItemTag().IsValid());

	UBH_ArmorItem* Item = NewObject<UBH_ArmorItem>(GetTransientPackage());
	if (!TestNotNull(TEXT("Transient armor item"), Item))
	{
		return false;
	}
	Item->ItemTags.AddTag(UBH_PlayerMenuLibrary::GetQuestItemTag());
	TestFalse(TEXT("Quest item refused"), UBH_PlayerMenuLibrary::CanDropItem(Item, Reason));
	TestFalse(TEXT("Refusal has a reason"), Reason.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_P13G_FirstOpenCueTest, "BlackwoodHollow.Phase13G.UI.FirstOpenCueOnce", BH_P13G_FLAGS)
bool FBH_P13G_FirstOpenCueTest::RunTest(const FString& Parameters)
{
	// The subsystem is ClassWithin ULocalPlayer, so it needs a LocalPlayer outer; ULocalPlayer is in turn ClassWithin UEngine,
	// so the throwaway LocalPlayer is created with GEngine as its outer (it is never registered with the engine).
	if (!TestNotNull(TEXT("GEngine"), GEngine))
	{
		return false;
	}
	ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine, NAME_None, RF_Transient);
	if (!TestNotNull(TEXT("Transient local player"), LocalPlayer))
	{
		return false;
	}
	UBH_UILayoutSubsystem* Subsystem = NewObject<UBH_UILayoutSubsystem>(LocalPlayer);
	if (!TestNotNull(TEXT("Transient subsystem"), Subsystem))
	{
		return false;
	}
	TestTrue(TEXT("First call hands out the cue"), Subsystem->ConsumeFirstOpenCue());
	TestFalse(TEXT("Second call does not"), Subsystem->ConsumeFirstOpenCue());
	TestFalse(TEXT("Third call does not"), Subsystem->ConsumeFirstOpenCue());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_P13G_RpcFlagsTest, "BlackwoodHollow.Phase13G.Component.ServerRpcs", BH_P13G_FLAGS)
bool FBH_P13G_RpcFlagsTest::RunTest(const FString& Parameters)
{
	const UClass* Class = UBH_PlayerMenuComponent::StaticClass();
	TestTrue(TEXT("ServerEquipItem is a reliable server RPC"), BHPhase13GTests::IsReliableServerRpc(Class, TEXT("ServerEquipItem")));
	TestTrue(TEXT("ServerUnequipSlot is a reliable server RPC"), BHPhase13GTests::IsReliableServerRpc(Class, TEXT("ServerUnequipSlot")));
	TestTrue(TEXT("ServerDropItem is a reliable server RPC"), BHPhase13GTests::IsReliableServerRpc(Class, TEXT("ServerDropItem")));

	const UFunction* Result = Class->FindFunctionByName(TEXT("ClientActionResult"));
	if (TestNotNull(TEXT("ClientActionResult exists"), Result))
	{
		TestTrue(TEXT("ClientActionResult is a client RPC"), Result->HasAnyFunctionFlags(FUNC_Net | FUNC_NetClient));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_P13G_PreviewDefaultsTest, "BlackwoodHollow.Phase13G.Preview.Defaults", BH_P13G_FLAGS)
bool FBH_P13G_PreviewDefaultsTest::RunTest(const FString& Parameters)
{
	const ABH_CharacterPreview* CDO = GetDefault<ABH_CharacterPreview>();
	TestFalse(TEXT("Preview is never replicated"), CDO->GetIsReplicated());
	TestFalse(TEXT("Not active by default"), CDO->IsPreviewActive());
	TestFalse(TEXT("Does not tick until activated"), CDO->PrimaryActorTick.bStartWithTickEnabled);
	if (TestNotNull(TEXT("Capture component"), CDO->Capture.Get()))
	{
		TestFalse(TEXT("Capture is not every frame"), CDO->Capture->bCaptureEveryFrame);
		TestFalse(TEXT("Capture is not on movement"), CDO->Capture->bCaptureOnMovement);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_P13G_CueFlagDefaultTest, "BlackwoodHollow.Phase13G.UI.CueFlagDefault", BH_P13G_FLAGS)
bool FBH_P13G_CueFlagDefaultTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("Base menu does not show the not-paused cue"), GetDefault<UBH_MenuActivatableWidget>(UBH_MenuActivatableWidget::StaticClass())->bShowNotPausedCue);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
