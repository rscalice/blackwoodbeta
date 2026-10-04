// Blackwood Hollow - Per-local-player HUD owner (implementation)

#include "UI/BH_HUDSubsystem.h"
#include "UI/BH_HUDWidget.h"
#include "UI/BH_BossHealthBarWidget.h"
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

namespace
{
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
	const APawn* LocalPawn = GetLocalPawn();
	AActor* Best = nullptr;
	if (LocalPawn)
	{
		double BestDistSq = TNumericLimits<double>::Max();
		for (TActorIterator<APawn> It(World); It; ++It)
		{
			APawn* Candidate = *It;
			const UBH_CombatIdentityComponent* Identity = UBH_CombatIdentityComponent::Find(Candidate);
			if (!Identity || !Identity->bIsBoss || Identity->GetAggroTarget() != LocalPawn || IsBossDead(Candidate))
			{
				continue;
			}
			const double DistSq = FVector::DistSquared(Candidate->GetActorLocation(), LocalPawn->GetActorLocation());
			if (DistSq < BestDistSq)
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
	else if (BossBar && BossBar->IsBarShown() && !Timers.IsTimerActive(BossHideTimer))
	{
		// The boss died, dropped aggro, or is gone: linger, then fade out.
		Timers.SetTimer(BossHideTimer, this, &UBH_HUDSubsystem::HideBossBar, FMath::Max(MainHUD->BossBarHideDelay, 0.01f), false);
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

	// Bottom-centre, lifted clear of the vitals cluster.
	BossBar->SetAnchorsInViewport(FAnchors(0.5f, 1.f));
	BossBar->SetAlignmentInViewport(FVector2D(0.5, 1.0));
	BossBar->SetPositionInViewport(FVector2D(0.0, -MainHUD->BossBarBottomOffset), /*bRemoveDPIScale*/ false);

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

void UBH_HUDSubsystem::Deinitialize()
{
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
