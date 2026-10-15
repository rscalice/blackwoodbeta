// Blackwood Hollow - Phase 13F-1 automation tests (hit feedback, sanctuary dedupe defaults, lunge assist, low health, crab surface)
// Run: Session Frontend > Automation > "BlackwoodHollow.Phase13F", or headless:
//   UnrealEditor-Cmd.exe <uproject> -ExecCmds="Automation RunTests BlackwoodHollow.Phase13F; Quit" -unattended -nullrhi -nosplash
// Pure data-contract and default-value tests on class default objects and plain structs: no map, no world, no PIE.
// Behaviour that needs a world (sanctuary overlap dedupe, the lunge warp itself, stamina-empty events) is on the manual PIE list in the
// Phase 13F-1 report, because the private helpers (ABH_WardenSanctuary::IsStrongerThan, UAH_GA_MeleeAttack_Base::ApplyLungeWarp) cannot be reached from here.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystem/Abilities/AH_GA_Block.h"
#include "AbilitySystem/Abilities/AH_GA_MeleeAttack_Base.h"
#include "Characters/BH_EnemyBase.h"
#include "Characters/BH_EnemyCrab.h"
#include "Combat/BH_CombatFeel.h"
#include "Combat/BH_CombatIdentityComponent.h"
#include "Combat/BH_HitFeedback.h"
#include "Combat/BH_LockOnComponent.h"
#include "Consumables/BH_WardenSanctuary.h"
#include "UI/BH_HUDWidget.h"
#include "UObject/UnrealType.h"

#define BH_P13F_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace BHPhase13FTests
{
	// True when the named property on Class is EditDefaultsOnly (editable, but not on placed instances).
	static bool IsEditDefaultsOnly(const UClass* Class, const FName PropertyName)
	{
		const FProperty* Prop = FindFProperty<FProperty>(Class, PropertyName);
		return Prop
			&& Prop->HasAnyPropertyFlags(CPF_Edit)
			&& Prop->HasAnyPropertyFlags(CPF_DisableEditOnInstance);
	}
}

// ---- Hit feedback enums ------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_P13F_HitEnumsTest, "BlackwoodHollow.Phase13F.HitFeedback.Enums", BH_P13F_FLAGS)
bool FBH_P13F_HitEnumsTest::RunTest(const FString& Parameters)
{
	// Impact tiers keep their order; Escalate(Massive) saturates.
	TestEqual(TEXT("Light = 0"), static_cast<int32>(EBH_ImpactTier::Light), 0);
	TestEqual(TEXT("Medium = 1"), static_cast<int32>(EBH_ImpactTier::Medium), 1);
	TestEqual(TEXT("Heavy = 2"), static_cast<int32>(EBH_ImpactTier::Heavy), 2);
	TestEqual(TEXT("Massive = 3"), static_cast<int32>(EBH_ImpactTier::Massive), 3);
	TestEqual(TEXT("Escalate Light -> Medium"), static_cast<int32>(UBH_CombatFeelLibrary::Escalate(EBH_ImpactTier::Light)), static_cast<int32>(EBH_ImpactTier::Medium));
	TestEqual(TEXT("Escalate Massive stays Massive"), static_cast<int32>(UBH_CombatFeelLibrary::Escalate(EBH_ImpactTier::Massive)), static_cast<int32>(EBH_ImpactTier::Massive));

	// Surface: Flesh, Armour, Shell (Flesh is the zero value, so a zeroed struct is Flesh).
	TestEqual(TEXT("Surface Flesh = 0"), static_cast<int32>(EBH_HitSurface::Flesh), 0);
	TestEqual(TEXT("Surface Armour = 1"), static_cast<int32>(EBH_HitSurface::Armour), 1);
	TestEqual(TEXT("Surface Shell = 2"), static_cast<int32>(EBH_HitSurface::Shell), 2);

	// Outcome: Hit, Block, PerfectBlock, PostureBreak.
	TestEqual(TEXT("Outcome Hit = 0"), static_cast<int32>(EBH_HitOutcome::Hit), 0);
	TestEqual(TEXT("Outcome Block = 1"), static_cast<int32>(EBH_HitOutcome::Block), 1);
	TestEqual(TEXT("Outcome PerfectBlock = 2"), static_cast<int32>(EBH_HitOutcome::PerfectBlock), 2);
	TestEqual(TEXT("Outcome PostureBreak = 3"), static_cast<int32>(EBH_HitOutcome::PostureBreak), 3);

	// Reflection names (Blueprint-facing): three surfaces and four outcomes, plus the hidden _MAX entry.
	const UEnum* SurfaceEnum = StaticEnum<EBH_HitSurface>();
	if (TestNotNull(TEXT("EBH_HitSurface reflected"), SurfaceEnum))
	{
		TestEqual(TEXT("Surface entry count"), SurfaceEnum->NumEnums() - 1, 3);
		TestEqual(TEXT("Shell name"), SurfaceEnum->GetNameStringByValue(static_cast<int64>(EBH_HitSurface::Shell)), FString(TEXT("Shell")));
	}
	const UEnum* OutcomeEnum = StaticEnum<EBH_HitOutcome>();
	if (TestNotNull(TEXT("EBH_HitOutcome reflected"), OutcomeEnum))
	{
		TestEqual(TEXT("Outcome entry count"), OutcomeEnum->NumEnums() - 1, 4);
		TestEqual(TEXT("PerfectBlock name"), OutcomeEnum->GetNameStringByValue(static_cast<int64>(EBH_HitOutcome::PerfectBlock)), FString(TEXT("PerfectBlock")));
	}
	return true;
}

// ---- Hit feedback payload defaults -------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_P13F_HitStructTest, "BlackwoodHollow.Phase13F.HitFeedback.StructDefaults", BH_P13F_FLAGS)
bool FBH_P13F_HitStructTest::RunTest(const FString& Parameters)
{
	const FBH_HitFeedback Feedback;
	TestNull(TEXT("Attacker null"), Feedback.Attacker.Get());
	TestNull(TEXT("Victim null"), Feedback.Victim.Get());
	TestEqual(TEXT("Tier Light"), static_cast<int32>(Feedback.Tier), static_cast<int32>(EBH_ImpactTier::Light));
	TestEqual(TEXT("Surface Flesh"), static_cast<int32>(Feedback.Surface), static_cast<int32>(EBH_HitSurface::Flesh));
	TestEqual(TEXT("Outcome Hit"), static_cast<int32>(Feedback.Outcome), static_cast<int32>(EBH_HitOutcome::Hit));
	TestFalse(TEXT("Not blighted"), Feedback.bIsBlighted);
	TestFalse(TEXT("Not fatal"), Feedback.bFatal);
	TestTrue(TEXT("Location zero"), Feedback.Location.IsZero());
	TestTrue(TEXT("Direction zero"), Feedback.Direction.IsZero());
	TestFalse(TEXT("Attacker not local"), Feedback.bAttackerLocallyControlled);
	TestFalse(TEXT("Victim not local"), Feedback.bVictimLocallyControlled);
	return true;
}

// The library falls back to Flesh / not blighted for no actor (players and unknown actors).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_P13F_HitLibraryTest, "BlackwoodHollow.Phase13F.HitFeedback.LibraryFallbacks", BH_P13F_FLAGS)
bool FBH_P13F_HitLibraryTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("GetHitSurface(null) is Flesh"), static_cast<int32>(UBH_HitFeedbackLibrary::GetHitSurface(nullptr)), static_cast<int32>(EBH_HitSurface::Flesh));
	TestFalse(TEXT("IsActorBlighted(null) is false"), UBH_HitFeedbackLibrary::IsActorBlighted(nullptr));
	TestNull(TEXT("No subsystem without a world context"), UBH_HitFeedbackSubsystem::GetHitFeedbackSubsystem(nullptr));
	return true;
}

// ---- Surface type defaults ---------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_P13F_SurfaceDefaultsTest, "BlackwoodHollow.Phase13F.Surface.Defaults", BH_P13F_FLAGS)
bool FBH_P13F_SurfaceDefaultsTest::RunTest(const FString& Parameters)
{
	const UBH_CombatIdentityComponent* Identity = GetDefault<UBH_CombatIdentityComponent>();
	if (TestNotNull(TEXT("Identity component CDO"), Identity))
	{
		TestEqual(TEXT("Identity default surface is Flesh"), static_cast<int32>(Identity->SurfaceType), static_cast<int32>(EBH_HitSurface::Flesh));
		TestFalse(TEXT("Identity default is not blighted"), Identity->bBlighted);
	}

	const ABH_EnemyBase* Enemy = GetDefault<ABH_EnemyBase>();
	if (TestNotNull(TEXT("Enemy base CDO"), Enemy))
	{
		TestEqual(TEXT("Enemy base default surface is Flesh"), static_cast<int32>(Enemy->SurfaceType), static_cast<int32>(EBH_HitSurface::Flesh));
		TestFalse(TEXT("Enemy base default is not blighted"), Enemy->bBlighted);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_P13F_CrabSurfaceTest, "BlackwoodHollow.Phase13F.Surface.CrabIsShellAndBlighted", BH_P13F_FLAGS)
bool FBH_P13F_CrabSurfaceTest::RunTest(const FString& Parameters)
{
	const ABH_EnemyCrab* Crab = GetDefault<ABH_EnemyCrab>();
	if (!TestNotNull(TEXT("Crab CDO"), Crab)) { return false; }

	TestEqual(TEXT("Crab surface is Shell"), static_cast<int32>(Crab->SurfaceType), static_cast<int32>(EBH_HitSurface::Shell));
	TestTrue(TEXT("Crab is blighted"), Crab->bBlighted);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_P13F_SurfaceFlagsTest, "BlackwoodHollow.Phase13F.Surface.EditorFlags", BH_P13F_FLAGS)
bool FBH_P13F_SurfaceFlagsTest::RunTest(const FString& Parameters)
{
	// Enemy class defaults are EditDefaultsOnly (the same on every machine, nothing replicates).
	TestTrue(TEXT("ABH_EnemyBase::SurfaceType EditDefaultsOnly"), BHPhase13FTests::IsEditDefaultsOnly(ABH_EnemyBase::StaticClass(), TEXT("SurfaceType")));
	TestTrue(TEXT("ABH_EnemyBase::bBlighted EditDefaultsOnly"), BHPhase13FTests::IsEditDefaultsOnly(ABH_EnemyBase::StaticClass(), TEXT("bBlighted")));

	const FProperty* EnemySurface = FindFProperty<FProperty>(ABH_EnemyBase::StaticClass(), TEXT("SurfaceType"));
	if (TestNotNull(TEXT("SurfaceType reflected"), EnemySurface))
	{
		TestFalse(TEXT("SurfaceType does not replicate"), EnemySurface->HasAnyPropertyFlags(CPF_Net));
	}

	// The identity component's copies are EditAnywhere (set per enemy Blueprint) and also not replicated.
	const FProperty* IdentitySurface = FindFProperty<FProperty>(UBH_CombatIdentityComponent::StaticClass(), TEXT("SurfaceType"));
	if (TestNotNull(TEXT("Identity SurfaceType reflected"), IdentitySurface))
	{
		TestTrue(TEXT("Identity SurfaceType editable"), IdentitySurface->HasAnyPropertyFlags(CPF_Edit));
		TestFalse(TEXT("Identity SurfaceType does not replicate"), IdentitySurface->HasAnyPropertyFlags(CPF_Net));
	}
	return true;
}

// ---- Low health threshold ----------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_P13F_LowHealthDefaultTest, "BlackwoodHollow.Phase13F.LowHealth.NativeDefault", BH_P13F_FLAGS)
bool FBH_P13F_LowHealthDefaultTest::RunTest(const FString& Parameters)
{
	// The native default (the DA_CombatFeel asset may tune it, so the class default object is checked, not the asset).
	const UBH_CombatFeelSettings* Settings = GetDefault<UBH_CombatFeelSettings>();
	if (!TestNotNull(TEXT("CombatFeelSettings CDO"), Settings)) { return false; }
	TestEqual(TEXT("LowHealthThreshold defaults to 0.30"), Settings->LowHealthThreshold, 0.30f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_P13F_LowHealthRuleTest, "BlackwoodHollow.Phase13F.LowHealth.IsLowHealth", BH_P13F_FLAGS)
bool FBH_P13F_LowHealthRuleTest::RunTest(const FString& Parameters)
{
	const float Threshold = UBH_CombatFeelLibrary::GetLowHealthThreshold();
	TestTrue(TEXT("Threshold is within 0..1"), Threshold >= 0.f && Threshold <= 1.f);

	const float MaxHealth = 200.f;
	const float Edge = MaxHealth * Threshold;

	if (Edge > 0.f)
	{
		TestTrue(TEXT("Exactly at the threshold is low"), UBH_CombatFeelLibrary::IsLowHealth(Edge, MaxHealth));
		TestTrue(TEXT("A sliver of health is low"), UBH_CombatFeelLibrary::IsLowHealth(0.01f, MaxHealth));
	}
	if (Threshold < 1.f)
	{
		TestFalse(TEXT("Just above the threshold is not low"), UBH_CombatFeelLibrary::IsLowHealth(Edge + 1.f, MaxHealth));
		TestFalse(TEXT("Full health is not low"), UBH_CombatFeelLibrary::IsLowHealth(MaxHealth, MaxHealth));
	}
	TestFalse(TEXT("Dead (0 health) is not low"), UBH_CombatFeelLibrary::IsLowHealth(0.f, MaxHealth));
	TestFalse(TEXT("Negative health is not low"), UBH_CombatFeelLibrary::IsLowHealth(-5.f, MaxHealth));
	TestFalse(TEXT("Unset max health is not low"), UBH_CombatFeelLibrary::IsLowHealth(10.f, 0.f));
	TestFalse(TEXT("Negative max health is not low"), UBH_CombatFeelLibrary::IsLowHealth(10.f, -100.f));
	return true;
}

// ---- Lunge assist ------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_P13F_LungeDefaultsTest, "BlackwoodHollow.Phase13F.Lunge.NativeDefaults", BH_P13F_FLAGS)
bool FBH_P13F_LungeDefaultsTest::RunTest(const FString& Parameters)
{
	const UAH_GA_MeleeAttack_Base* Melee = GetDefault<UAH_GA_MeleeAttack_Base>();
	if (!TestNotNull(TEXT("MeleeAttack_Base CDO"), Melee)) { return false; }

	TestTrue(TEXT("Lunge assist enabled"), Melee->bEnableLungeAssist);
	TestEqual(TEXT("Max correction 150 cm (1.5 m)"), Melee->LungeMaxCorrection, 150.f);
	TestEqual(TEXT("Max angle 45 degrees"), Melee->LungeMaxAngleDegrees, 45.f);
	TestEqual(TEXT("Expected reach 120 cm"), Melee->LungeExpectedReach, 120.f);
	TestEqual(TEXT("Stop gap 20 cm"), Melee->LungeStopGap, 20.f);
	TestEqual(TEXT("Warp target name"), Melee->LungeWarpTargetName, FName(TEXT("BH_Lunge")));

	// Facing for both target kinds stays on (soft target + hard lock feed the lunge).
	TestTrue(TEXT("Faces the soft target on activate"), Melee->bFaceSoftTargetOnActivate);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_P13F_LungeFlagsTest, "BlackwoodHollow.Phase13F.Lunge.EditorFlagsAndClamps", BH_P13F_FLAGS)
bool FBH_P13F_LungeFlagsTest::RunTest(const FString& Parameters)
{
	const UClass* Cls = UAH_GA_MeleeAttack_Base::StaticClass();
	static const TCHAR* const Names[] = {
		TEXT("bEnableLungeAssist"), TEXT("LungeMaxCorrection"), TEXT("LungeMaxAngleDegrees"),
		TEXT("LungeExpectedReach"), TEXT("LungeStopGap"), TEXT("LungeWarpTargetName") };
	for (const TCHAR* Name : Names)
	{
		TestTrue(*FString::Printf(TEXT("%s is EditDefaultsOnly"), Name), BHPhase13FTests::IsEditDefaultsOnly(Cls, FName(Name)));
	}

	// The angle cap is bounded to a half turn so a designer cannot enter a nonsense value.
	const FProperty* AngleProp = FindFProperty<FProperty>(Cls, TEXT("LungeMaxAngleDegrees"));
	if (TestNotNull(TEXT("LungeMaxAngleDegrees reflected"), AngleProp))
	{
		TestEqual(TEXT("Angle ClampMin"), AngleProp->GetMetaData(TEXT("ClampMin")), FString(TEXT("0.0")));
		TestEqual(TEXT("Angle ClampMax"), AngleProp->GetMetaData(TEXT("ClampMax")), FString(TEXT("180.0")));
	}
	return true;
}

// The lock-on component starts with no melee target and no turn (the lunge reads these right after facing).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_P13F_LockOnDefaultsTest, "BlackwoodHollow.Phase13F.Lunge.LockOnLastTargetEmpty", BH_P13F_FLAGS)
bool FBH_P13F_LockOnDefaultsTest::RunTest(const FString& Parameters)
{
	const UBH_LockOnComponent* LockOn = GetDefault<UBH_LockOnComponent>();
	if (!TestNotNull(TEXT("LockOn CDO"), LockOn)) { return false; }

	TestNull(TEXT("No last melee target"), LockOn->GetLastMeleeTarget());
	TestEqual(TEXT("No last melee turn"), LockOn->GetLastMeleeTurnDegrees(), 0.f);
	return true;
}

// ---- Perfect block window (13F tunes ParryWindow by feel; the default stays 0.2) ---------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_P13F_ParryWindowTest, "BlackwoodHollow.Phase13F.PerfectBlock.ParryWindowDefault", BH_P13F_FLAGS)
bool FBH_P13F_ParryWindowTest::RunTest(const FString& Parameters)
{
	const UAH_GA_Block* Block = GetDefault<UAH_GA_Block>();
	if (!TestNotNull(TEXT("UAH_GA_Block CDO"), Block)) { return false; }

	TestEqual(TEXT("ParryWindow default 0.2 s"), Block->ParryWindow, 0.2f);
	TestTrue(TEXT("ParryWindow is EditDefaultsOnly"), BHPhase13FTests::IsEditDefaultsOnly(UAH_GA_Block::StaticClass(), TEXT("ParryWindow")));

	const FProperty* WindowProp = FindFProperty<FProperty>(UAH_GA_Block::StaticClass(), TEXT("ParryWindow"));
	if (TestNotNull(TEXT("ParryWindow reflected"), WindowProp))
	{
		TestEqual(TEXT("ParryWindow ClampMin"), WindowProp->GetMetaData(TEXT("ClampMin")), FString(TEXT("0.0")));
	}
	return true;
}

// ---- Warden sanctuary dedupe -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_P13F_SanctuaryDefaultsTest, "BlackwoodHollow.Phase13F.Sanctuary.Defaults", BH_P13F_FLAGS)
bool FBH_P13F_SanctuaryDefaultsTest::RunTest(const FString& Parameters)
{
	const ABH_WardenSanctuary* Sanctuary = GetDefault<ABH_WardenSanctuary>();
	if (!TestNotNull(TEXT("Sanctuary CDO"), Sanctuary)) { return false; }

	// Incense is the default source type, so two Incense sanctuaries dedupe; a different type would still stack.
	TestEqual(TEXT("Default SourceType is Incense"), Sanctuary->SourceType, FName(TEXT("Incense")));
	TestEqual(TEXT("Default BlightReductionFraction (Incense halves build-up)"), Sanctuary->BlightReductionFraction, 0.5f);
	TestEqual(TEXT("Default UpdateInterval"), Sanctuary->UpdateInterval, 0.2f);
	TestTrue(TEXT("Radius is positive"), Sanctuary->Radius > 0.f);
	return true;
}

// ---- HUD stamina-empty state -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBH_P13F_StaminaDefaultsTest, "BlackwoodHollow.Phase13F.Hud.StaminaEmptyDefaults", BH_P13F_FLAGS)
bool FBH_P13F_StaminaDefaultsTest::RunTest(const FString& Parameters)
{
	const UBH_HUDWidget* Hud = GetDefault<UBH_HUDWidget>();
	if (!TestNotNull(TEXT("HUD widget CDO"), Hud)) { return false; }

	TestEqual(TEXT("StaminaRecoveredFraction defaults to 0.25"), Hud->StaminaRecoveredFraction, 0.25f);
	TestFalse(TEXT("Stamina not empty on a fresh widget"), Hud->IsStaminaEmpty());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
