// Blackwood Hollow - Per-local-player HUD owner (implementation)

#include "UI/BH_HUDSubsystem.h"
#include "UI/BH_HUDWidget.h"
#include "UI/BH_BossHealthBarWidget.h"
#include "AI/BH_AIController.h"
#include "Combat/BH_CombatIdentityComponent.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Blueprint/UserWidget.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Engine/GameViewportClient.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	/** A boss the local player is already presented stays shown this far (cm) beyond its aggro radius, so standing on the edge does not flicker the bar. */
	constexpr double BossBarRangeHysteresis = 100.0;

	/**
	 * The distance inside which the boss engages: the AggroRange of its own AI brain. The brain only exists on the server, so a
	 * client reads the same value from the pawn's AIControllerClass defaults (the radius is a class default, never set per instance).
	 * 0 = no brain found, so the bar never shows for that boss.
	 */
	float GetBossAggroRadius(const APawn* Boss)
	{
		if (!Boss)
		{
			return 0.f;
		}
		if (const ABH_AIController* Brain = Cast<ABH_AIController>(Boss->GetController()))
		{
			return Brain->AggroRange;
		}
		if (Boss->AIControllerClass && Boss->AIControllerClass->IsChildOf(ABH_AIController::StaticClass()))
		{
			if (const ABH_AIController* Defaults = Cast<ABH_AIController>(Boss->AIControllerClass->GetDefaultObject()))
			{
				return Defaults->AggroRange;
			}
		}
		return 0.f;
	}

	/** Dead = the loose Dead tag, or Health <= 0 (the tag may not reach clients; Health does). */
	bool IsBossDead(const AActor* Boss)
	{
		const UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(const_cast<AActor*>(Boss));
		if (!ASC)
		{
			return false;
		}
		if (ASC->HasMatchingGameplayTag(TAG_State_Combat_Dead))
		{
			return true;
		}
		return ASC->HasAttributeSetForAttribute(UAH_AttributeSet::GetHealthAttribute())
			&& ASC->GetNumericAttribute(UAH_AttributeSet::GetHealthAttribute()) <= 0.f;
	}
}

bool UBH_HUDSubsystem::SetupHUD(APawn* Pawn, TSubclassOf<UBH_HUDWidget> MainHUDClass, TSubclassOf<UUserWidget> DebugHUDClass)
{
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	APlayerController* PC = LocalPlayer ? LocalPlayer->GetPlayerController(Pawn ? Pawn->GetWorld() : nullptr) : nullptr;
	if (!Pawn || !PC || Pawn->GetController() != PC)
	{
		return false;
	}

	if (!MainHUD && MainHUDClass)
	{
		MainHUD = CreateWidget<UBH_HUDWidget>(PC, MainHUDClass);
		if (MainHUD)
		{
			MainHUD->AddToViewport(0);
		}
	}
	if (!DebugHUD && DebugHUDClass)
	{
		DebugHUD = CreateWidget<UUserWidget>(PC, DebugHUDClass);
		if (DebugHUD)
		{
			DebugHUD->AddToViewport(1);
		}
	}

	// Bind after AddToViewport so NativeConstruct has already run.
	if (MainHUD)
	{
		UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Pawn);
		if (!ASC)
		{
			// Blueprint characters may carry the component without implementing IAbilitySystemInterface.
			ASC = Pawn->FindComponentByClass<UAbilitySystemComponent>();
		}
		MainHUD->InitializeHUD(ASC);
		StartBossWatch();
	}

	ApplyVisibility();
	return MainHUD != nullptr;
}

void UBH_HUDSubsystem::SetDebugHUDVisible(bool bVisible)
{
	bDebugVisible = bVisible;
	ApplyVisibility();
}

void UBH_HUDSubsystem::ApplyVisibility()
{
	// With no debug HUD there is nothing to switch to: keep the production HUD up.
	const bool bShowDebug = bDebugVisible && DebugHUD;
	if (MainHUD)
	{
		MainHUD->SetVisibility(bShowDebug ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	}
	if (DebugHUD)
	{
		DebugHUD->SetVisibility(bShowDebug ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	}
	// The boss bar follows the production HUD: hidden while the debug HUD is up, back when it is not.
	if (BossBar && BossBar->IsBarShown())
	{
		BossBar->SetVisibility(bShowDebug ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
}

// ----------------------------------------------------------------------------
// Boss bar
// ----------------------------------------------------------------------------

APawn* UBH_HUDSubsystem::GetLocalPawn() const
{
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UWorld* World = LocalPlayer ? LocalPlayer->GetWorld() : nullptr;
	const APlayerController* PC = (LocalPlayer && World) ? LocalPlayer->GetPlayerController(World) : nullptr;
	return PC ? PC->GetPawn() : nullptr;
}

void UBH_HUDSubsystem::StartBossWatch()
{
	if (!MainHUD || !MainHUD->BossBarClass)
	{
		return; // no boss bar configured on WBP_HUD_Main
	}
	if (!AggroHandle.IsValid())
	{
		AggroHandle = UBH_CombatIdentityComponent::OnAnyAggroTargetChanged().AddUObject(this, &UBH_HUDSubsystem::OnAnyAggroChanged);
	}

	// The fallback scan lives on the current world's timer manager (a LocalPlayer outlives level travel; the world does not).
	UWorld* World = GetLocalPlayer() ? GetLocalPlayer()->GetWorld() : nullptr;
	if (WatchWorld.Get() != World)
	{
		if (UWorld* OldWorld = WatchWorld.Get())
		{
			OldWorld->GetTimerManager().ClearTimer(BossScanTimer);
			OldWorld->GetTimerManager().ClearTimer(BossHideTimer);
		}
		WatchWorld = World;
	}
	if (World && !World->GetTimerManager().IsTimerActive(BossScanTimer))
	{
		World->GetTimerManager().SetTimer(BossScanTimer, this, &UBH_HUDSubsystem::EvaluateBoss, FMath::Max(MainHUD->BossScanInterval, 0.1f), true);
	}
	EvaluateBoss();
}

void UBH_HUDSubsystem::StopBossWatch()
{
	if (AggroHandle.IsValid())
	{
		UBH_CombatIdentityComponent::OnAnyAggroTargetChanged().Remove(AggroHandle);
		AggroHandle.Reset();
	}
	if (UWorld* World = WatchWorld.Get())
	{
		World->GetTimerManager().ClearTimer(BossScanTimer);
		World->GetTimerManager().ClearTimer(BossHideTimer);
	}
	UnbindBossHealth();
	PresentedBoss.Reset();
}

void UBH_HUDSubsystem::OnAnyAggroChanged(UBH_CombatIdentityComponent* Source, AActor* NewTarget)
{
	// The delegate is process-wide (every PIE world): only react to bosses in this player's world.
	if (Source && GetLocalPlayer() && Source->GetWorld() == GetLocalPlayer()->GetWorld())
	{
		EvaluateBoss();
	}
}

void UBH_HUDSubsystem::EvaluateBoss()
{
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UWorld* World = LocalPlayer ? LocalPlayer->GetWorld() : nullptr;
	if (!World || !MainHUD || !MainHUD->BossBarClass)
	{
		return;
	}

	// Nearest living boss that is fighting the local pawn.
	// Nearest living, engaged boss whose own aggro radius contains the local pawn. Every local player decides from their OWN pawn.
	const APawn* LocalPawn = GetLocalPawn();
	const AActor* Presented = PresentedBoss.Get();
	AActor* Best = nullptr;
	if (LocalPawn)
	{
		double BestDistSq = TNumericLimits<double>::Max();
		for (TActorIterator<APawn> It(World); It; ++It)
		{
			APawn* Candidate = *It;
			const UBH_CombatIdentityComponent* Identity = UBH_CombatIdentityComponent::Find(Candidate);
			// Engaged = the boss AI has a target (AggroTarget is replicated and cleared when the boss loses aggro).
			if (!Identity || !Identity->bIsBoss || !Identity->GetAggroTarget() || IsBossDead(Candidate))
			{
				continue;
			}
			const double Radius = static_cast<double>(GetBossAggroRadius(Candidate)) + (Candidate == Presented ? BossBarRangeHysteresis : 0.0);
			const double DistSq = FVector::DistSquared(Candidate->GetActorLocation(), LocalPawn->GetActorLocation());
			if (DistSq <= Radius * Radius && DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				Best = Candidate;
			}
		}
	}

	FTimerManager& Timers = World->GetTimerManager();
	if (Best)
	{
		Timers.ClearTimer(BossHideTimer);
		if (PresentedBoss.Get() != Best || !BossBar || !BossBar->IsBarShown())
		{
			ShowBossBar(Best);
		}
	}
	else if (BossBar && BossBar->IsBarShown())
	{
		if (Presented && IsBossDead(Presented))
		{
			// Death: let the bar drain, then fade out (BossBarHideDelay, set to about 1 s on WBP_HUD_Main).
			if (!Timers.IsTimerActive(BossHideTimer))
			{
				Timers.SetTimer(BossHideTimer, this, &UBH_HUDSubsystem::HideBossBar, FMath::Max(MainHUD->BossBarHideDelay, 0.01f), false);
			}
		}
		else
		{
			// Lost aggro, or this player left the aggro radius: fade out right away.
			Timers.ClearTimer(BossHideTimer);
			HideBossBar();
		}
	}
}

void UBH_HUDSubsystem::ShowBossBar(AActor* Boss)
{
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UWorld* World = LocalPlayer ? LocalPlayer->GetWorld() : nullptr;
	APlayerController* PC = (LocalPlayer && World) ? LocalPlayer->GetPlayerController(World) : nullptr;
	if (!Boss || !PC || !MainHUD || !MainHUD->BossBarClass)
	{
		return;
	}

	if (!BossBar)
	{
		BossBar = CreateWidget<UBH_BossHealthBarWidget>(PC, MainHUD->BossBarClass);
		if (!BossBar)
		{
			return;
		}
		BossBar->AddToViewport(0);
	}

	// Top-centre. BossBarBottomOffset (a misnomer kept so no header changes) is now the distance in pixels from the TOP edge of the screen.
	BossBar->SetAnchorsInViewport(FAnchors(0.5f, 0.f));
	BossBar->SetAlignmentInViewport(FVector2D(0.5, 0.0));
	BossBar->SetPositionInViewport(FVector2D(0.0, MainHUD->BossBarBottomOffset), /*bRemoveDPIScale*/ false);

	BossBar->PresentBoss(Boss);
	PresentedBoss = Boss;
	BindBossHealth(Boss);
	ApplyVisibility();
}

void UBH_HUDSubsystem::HideBossBar()
{
	if (BossBar)
	{
		BossBar->DismissBar();
	}
	UnbindBossHealth();
	PresentedBoss.Reset();
}

void UBH_HUDSubsystem::BindBossHealth(AActor* Boss)
{
	UnbindBossHealth();
	UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Boss);
	if (ASC)
	{
		BossHealthHandle = ASC->GetGameplayAttributeValueChangeDelegate(UAH_AttributeSet::GetHealthAttribute()).AddUObject(this, &UBH_HUDSubsystem::OnBossHealthChanged);
		BossHealthASC = ASC;
	}
}

void UBH_HUDSubsystem::UnbindBossHealth()
{
	if (UAbilitySystemComponent* ASC = BossHealthASC.Get())
	{
		ASC->GetGameplayAttributeValueChangeDelegate(UAH_AttributeSet::GetHealthAttribute()).Remove(BossHealthHandle);
	}
	BossHealthHandle.Reset();
	BossHealthASC.Reset();
}

void UBH_HUDSubsystem::OnBossHealthChanged(const FOnAttributeChangeData& ChangeData)
{
	if (ChangeData.NewValue <= 0.f)
	{
		EvaluateBoss(); // schedules the hide right at the moment of death
	}
}

void UBH_HUDSubsystem::ShowLevelUp(int32 NewLevel)
{
	FBH_LevelUpInfo Info;
	Info.NewLevel = NewLevel;
	Info.PreviousLevel = FMath::Max(1, NewLevel - 1);
	ShowLevelUpDetailed(Info);
}

void UBH_HUDSubsystem::ShowLevelUpDetailed(const FBH_LevelUpInfo& Info)
{
	const int32 NewLevel = Info.NewLevel;
	if (MainHUD)
	{
		MainHUD->NotifyLevelUp(Info);
	}

	// A bound LevelUpBanner replaces the plain Slate text.
	const bool bNativeText = !MainHUD || (MainHUD->bShowNativeLevelUpText && !MainHUD->HasLevelUpBanner());
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UWorld* World = LocalPlayer ? LocalPlayer->GetWorld() : nullptr;
	UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
	if (!bNativeText || !Viewport)
	{
		return;
	}

	HideLevelUpText();
	LevelUpTextWidget = SNew(SBox)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Top)
		.Padding(FMargin(0.f, 140.f, 0.f, 0.f))
		.Visibility(EVisibility::HitTestInvisible)
		[
			SNew(STextBlock)
			.Text(FText::Format(NSLOCTEXT("BHHUD", "LevelUpText", "Level {0}"), FText::AsNumber(NewLevel)))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 40))
			.ColorAndOpacity(FLinearColor(1.f, 0.85f, 0.4f, 1.f))
		];
	Viewport->AddViewportWidgetContent(LevelUpTextWidget.ToSharedRef(), 20);

	LevelUpWorld = World;
	World->GetTimerManager().SetTimer(LevelUpTextTimer, this, &UBH_HUDSubsystem::HideLevelUpText, LevelUpTextSeconds, false);
}

void UBH_HUDSubsystem::HideLevelUpText()
{
	if (UWorld* World = LevelUpWorld.Get())
	{
		World->GetTimerManager().ClearTimer(LevelUpTextTimer);
		if (UGameViewportClient* Viewport = World->GetGameViewport())
		{
			if (LevelUpTextWidget.IsValid())
			{
				Viewport->RemoveViewportWidgetContent(LevelUpTextWidget.ToSharedRef());
			}
		}
	}
	LevelUpTextWidget.Reset();
}

void UBH_HUDSubsystem::Deinitialize()
{
	HideLevelUpText();
	StopBossWatch();
	if (BossBar)
	{
		BossBar->RemoveFromParent();
		BossBar = nullptr;
	}
	if (MainHUD)
	{
		MainHUD->RemoveFromParent();
		MainHUD = nullptr;
	}
	if (DebugHUD)
	{
		DebugHUD->RemoveFromParent();
		DebugHUD = nullptr;
	}
	Super::Deinitialize();
}

// ----------------------------------------------------------------------------
// Console: BH.HUD.ToggleDebug / BH.HUD.Debug 0|1 (every local player in the world)
// ----------------------------------------------------------------------------
namespace BH_HUDSubsystem_Private
{
	static void ForEachHUDSubsystem(UWorld* World, TFunctionRef<void(UBH_HUDSubsystem&)> Fn)
	{
		const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		if (!GameInstance)
		{
			return;
		}
		for (ULocalPlayer* LocalPlayer : GameInstance->GetLocalPlayers())
		{
			if (UBH_HUDSubsystem* HUD = LocalPlayer ? LocalPlayer->GetSubsystem<UBH_HUDSubsystem>() : nullptr)
			{
				Fn(*HUD);
			}
		}
	}

	static FAutoConsoleCommandWithWorld CmdToggleDebugHUD(
		TEXT("BH.HUD.ToggleDebug"),
		TEXT("Switch between the production HUD (WBP_HUD_Main) and the combat debug HUD (W_BH_DebugHUD)."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			ForEachHUDSubsystem(World, [](UBH_HUDSubsystem& HUD) { HUD.ToggleDebugHUD(); });
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdSetDebugHUD(
		TEXT("BH.HUD.Debug"),
		TEXT("BH.HUD.Debug 1 shows the combat debug HUD, 0 shows the production HUD."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			const bool bShow = Args.Num() > 0 ? FCString::Atoi(*Args[0]) != 0 : true;
			ForEachHUDSubsystem(World, [bShow](UBH_HUDSubsystem& HUD) { HUD.SetDebugHUDVisible(bShow); });
		}));
}
