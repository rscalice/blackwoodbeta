// Blackwood Hollow - HUD widget base (implementation)

#include "UI/BH_HUDWidget.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystem/BH_CombatFunctionLibrary.h"
#include "UI/BH_LevelUpBannerWidget.h"
#include "UI/BH_BossHealthBarWidget.h"
#include "UI/BH_HUDElements.h"
#include "Components/PanelWidget.h"
#include "Components/OverlaySlot.h"
#include "Components/VerticalBoxSlot.h"
#include "AbilitySystemComponent.h"
#include "Blueprint/WidgetTree.h"
#include "UObject/UObjectIterator.h"
#include "GameFramework/PlayerController.h"

void UBH_HUDWidget::InitializeHUD(UAbilitySystemComponent* ASC)
{
	UnbindFromASC();
	BoundASC = ASC;

	// Nested HUD widgets (e.g. WBP_VitalsCluster inside WBP_HUD_Main) follow their parent.
	if (WidgetTree)
	{
		WidgetTree->ForEachWidget([ASC](UWidget* Widget)
		{
			if (UBH_HUDWidget* Child = Cast<UBH_HUDWidget>(Widget))
			{
				Child->bIsNestedHUD = true;
				if (!Child->bExcludeFromParentInit)
				{
					Child->InitializeHUD(ASC);
				}
			}
		});
	}

	if (!ASC)
	{
		return;
	}

	HealthHandle = ASC->GetGameplayAttributeValueChangeDelegate(UAH_AttributeSet::GetHealthAttribute()).AddUObject(this, &UBH_HUDWidget::OnAttributeChanged);
	MaxHealthHandle = ASC->GetGameplayAttributeValueChangeDelegate(UAH_AttributeSet::GetMaxHealthAttribute()).AddUObject(this, &UBH_HUDWidget::OnAttributeChanged);
	StaminaHandle = ASC->GetGameplayAttributeValueChangeDelegate(UAH_AttributeSet::GetStaminaAttribute()).AddUObject(this, &UBH_HUDWidget::OnAttributeChanged);
	MaxStaminaHandle = ASC->GetGameplayAttributeValueChangeDelegate(UAH_AttributeSet::GetMaxStaminaAttribute()).AddUObject(this, &UBH_HUDWidget::OnAttributeChanged);
	PostureHandle = ASC->GetGameplayAttributeValueChangeDelegate(UAH_AttributeSet::GetPostureAttribute()).AddUObject(this, &UBH_HUDWidget::OnAttributeChanged);
	MaxPostureHandle = ASC->GetGameplayAttributeValueChangeDelegate(UAH_AttributeSet::GetMaxPostureAttribute()).AddUObject(this, &UBH_HUDWidget::OnAttributeChanged);
	PostureBrokenTagHandle = ASC->RegisterGameplayTagEvent(TAG_State_Combat_PostureBroken, EGameplayTagEventType::NewOrRemoved)
		.AddUObject(this, &UBH_HUDWidget::OnPostureBrokenTagChanged);

	// Push the current state once so the widget is right before the first change.
	PushHealth();
	PushStamina();
	PushPosture();
	bPostureBroken = !bPostureBroken; // force the first refresh to broadcast
	RefreshPostureBroken();

	const FString Stance = UBH_CombatFunctionLibrary::GetCurrentOverlayPoseDisplayName(ASC->GetAvatarActor());
	if (!Stance.IsEmpty())
	{
		CurrentStanceName.Reset();
		NotifyStanceChanged(Stance);
	}
}

UAbilitySystemComponent* UBH_HUDWidget::GetHUDAbilitySystem() const
{
	return BoundASC.Get();
}

float UBH_HUDWidget::GetHealth() const
{
	const UAbilitySystemComponent* ASC = BoundASC.Get();
	return ASC ? ASC->GetNumericAttribute(UAH_AttributeSet::GetHealthAttribute()) : 0.f;
}

float UBH_HUDWidget::GetMaxHealth() const
{
	const UAbilitySystemComponent* ASC = BoundASC.Get();
	return ASC ? ASC->GetNumericAttribute(UAH_AttributeSet::GetMaxHealthAttribute()) : 0.f;
}

float UBH_HUDWidget::GetStamina() const
{
	const UAbilitySystemComponent* ASC = BoundASC.Get();
	return ASC ? ASC->GetNumericAttribute(UAH_AttributeSet::GetStaminaAttribute()) : 0.f;
}

float UBH_HUDWidget::GetMaxStamina() const
{
	const UAbilitySystemComponent* ASC = BoundASC.Get();
	return ASC ? ASC->GetNumericAttribute(UAH_AttributeSet::GetMaxStaminaAttribute()) : 0.f;
}

float UBH_HUDWidget::GetPosture() const
{
	const UAbilitySystemComponent* ASC = BoundASC.Get();
	return ASC ? ASC->GetNumericAttribute(UAH_AttributeSet::GetPostureAttribute()) : 0.f;
}

float UBH_HUDWidget::GetMaxPosture() const
{
	const UAbilitySystemComponent* ASC = BoundASC.Get();
	return ASC ? ASC->GetNumericAttribute(UAH_AttributeSet::GetMaxPostureAttribute()) : 0.f;
}

void UBH_HUDWidget::OnAttributeChanged(const FOnAttributeChangeData& ChangeData)
{
	if (ChangeData.Attribute == UAH_AttributeSet::GetHealthAttribute() || ChangeData.Attribute == UAH_AttributeSet::GetMaxHealthAttribute())
	{
		PushHealth();
	}
	else if (ChangeData.Attribute == UAH_AttributeSet::GetStaminaAttribute() || ChangeData.Attribute == UAH_AttributeSet::GetMaxStaminaAttribute())
	{
		PushStamina();
	}
	else
	{
		PushPosture();
		RefreshPostureBroken();
	}
}

void UBH_HUDWidget::OnPostureBrokenTagChanged(const FGameplayTag Tag, int32 NewCount)
{
	RefreshPostureBroken();
}

void UBH_HUDWidget::PushHealth()
{
	const float Health = GetHealth();
	const float MaxHealth = GetMaxHealth();
	const float Percent = MaxHealth > 0.f ? FMath::Clamp(Health / MaxHealth, 0.f, 1.f) : 0.f;
	HandleHealthChanged(Health, MaxHealth);
	K2_OnHealthUpdated(Health, MaxHealth, Percent);
}

void UBH_HUDWidget::PushStamina()
{
	const float Stamina = GetStamina();
	const float MaxStamina = GetMaxStamina();
	const float Percent = MaxStamina > 0.f ? FMath::Clamp(Stamina / MaxStamina, 0.f, 1.f) : 0.f;
	HandleStaminaChanged(Stamina, MaxStamina);
	K2_OnStaminaUpdated(Stamina, MaxStamina, Percent);
}

void UBH_HUDWidget::PushPosture()
{
	const float Posture = GetPosture();
	const float MaxPosture = GetMaxPosture();
	const float Percent = MaxPosture > 0.f ? FMath::Clamp(Posture / MaxPosture, 0.f, 1.f) : 0.f;
	HandlePostureChanged(Posture, MaxPosture);
	K2_OnPostureUpdated(Posture, MaxPosture, Percent);
}

void UBH_HUDWidget::RefreshPostureBroken()
{
	const UAbilitySystemComponent* ASC = BoundASC.Get();
	// The tag is authoritative; Posture <= 0 covers the frames before a client sees the tag.
	const bool bBroken = ASC && (ASC->HasMatchingGameplayTag(TAG_State_Combat_PostureBroken) || GetPosture() <= 0.f);
	if (bBroken == bPostureBroken)
	{
		return;
	}
	bPostureBroken = bBroken;
	HandlePostureBrokenChanged(bBroken);
	K2_OnPostureBrokenChanged(bBroken);
}

void UBH_HUDWidget::NotifyLevelUp(int32 NewLevel)
{
	// Legacy entry point: no previous level / stat gains known.
	FBH_LevelUpInfo Info;
	Info.NewLevel = NewLevel;
	Info.PreviousLevel = FMath::Max(1, NewLevel - 1);
	NotifyLevelUp(Info);
}

void UBH_HUDWidget::NotifyLevelUp(const FBH_LevelUpInfo& Info)
{
	HandleLevelUpDetailed(Info);
	K2_OnLevelUp(Info.NewLevel); // always fired, with or without a banner

	if (WidgetTree)
	{
		WidgetTree->ForEachWidget([&Info](UWidget* Widget)
		{
			if (UBH_HUDWidget* Child = Cast<UBH_HUDWidget>(Widget))
			{
				Child->NotifyLevelUp(Info);
			}
		});
	}
}

bool UBH_HUDWidget::HasLevelUpBanner() const
{
	return LevelUpBanner != nullptr;
}

void UBH_HUDWidget::HandleLevelUpDetailed(const FBH_LevelUpInfo& Info)
{
	if (LevelUpBanner)
	{
		LevelUpBanner->ShowLevelUp(Info);
	}
	HandleLevelUp(Info.NewLevel);
}

void UBH_HUDWidget::NotifyStanceChanged(const FString& StanceName)
{
	if (StanceName.Equals(CurrentStanceName, ESearchCase::CaseSensitive))
	{
		return;
	}
	CurrentStanceName = StanceName;
	HandleStanceChanged(StanceName);
	K2_OnStanceUpdated(StanceName);
}

void UBH_HUDWidget::BroadcastStanceChanged(const AActor* Character, const FString& StanceName)
{
	if (!Character)
	{
		return;
	}
	for (TObjectIterator<UBH_HUDWidget> It; It; ++It)
	{
		UBH_HUDWidget* Widget = *It;
		if (!Widget || Widget->IsTemplate() || !Widget->BoundASC.IsValid())
		{
			continue;
		}
		if (Widget->BoundASC->GetAvatarActor() == Character)
		{
			Widget->NotifyStanceChanged(StanceName);
		}
	}
}

void UBH_HUDWidget::HandleLockedTargetChanged(AActor* Target)
{
	LockedTarget = Target;
	RefreshTopCentreLayout();
}

void UBH_HUDWidget::NativeConstruct()
{
	Super::NativeConstruct();
	RefreshTopCentreLayout(); // overwrites any render scale / padding saved on the asset with the no-boss state
}

bool UBH_HUDWidget::AttachBossBar(UBH_BossHealthBarWidget* Bar)
{
	if (!Bar || !BossBarSlot)
	{
		return false;
	}
	if (Bar->GetParent() == BossBarSlot)
	{
		return true;
	}
	Bar->RemoveFromParent(); // e.g. it was added to the viewport by the fallback path earlier
	UPanelSlot* PanelSlot = BossBarSlot->AddChild(Bar);
	if (!PanelSlot)
	{
		return false;
	}
	if (UOverlaySlot* OverlaySlot = Cast<UOverlaySlot>(PanelSlot))
	{
		OverlaySlot->SetHorizontalAlignment(HAlign_Center);
		OverlaySlot->SetVerticalAlignment(VAlign_Top);
	}
	return true;
}

void UBH_HUDWidget::SetPresentedBoss(AActor* Boss)
{
	if (PresentedBoss.Get() == Boss)
	{
		return;
	}
	PresentedBoss = Boss;
	RefreshTopCentreLayout();
}

void UBH_HUDWidget::RefreshTopCentreLayout()
{
	if (!TargetVitals)
	{
		return;
	}
	const AActor* Boss = PresentedBoss.Get();
	const bool bBossShown = Boss != nullptr;

	// Boss shown and the lock is on that boss: the bar already shows it, so the target panel hides.
	TargetVitals->SetSuppressedByBoss(bBossShown && LockedTarget.Get() == Boss);

	// Normal size alone; scaled around its top-centre under the bar (it stays centred and tight under the bar).
	TargetVitals->SetRenderTransformPivot(FVector2D(0.5, 0.0));
	TargetVitals->SetRenderScale(FVector2D(bBossShown ? TargetScaleUnderBoss : 1.f));
	if (UVerticalBoxSlot* StackSlot = Cast<UVerticalBoxSlot>(TargetVitals->Slot))
	{
		StackSlot->SetPadding(FMargin(0.f, bBossShown ? TargetGapUnderBoss : 0.f, 0.f, 0.f));
	}
}

void UBH_HUDWidget::NotifyLockedTargetChanged(AActor* Target)
{
	HandleLockedTargetChanged(Target);
	K2_OnLockedTargetChanged(Target);
}

void UBH_HUDWidget::BroadcastLockedTargetChanged(const APlayerController* PC, AActor* Target)
{
	if (!PC)
	{
		return;
	}
	for (TObjectIterator<UBH_HUDWidget> It; It; ++It)
	{
		UBH_HUDWidget* Widget = *It;
		if (!Widget || Widget->IsTemplate() || !Widget->GetWorld())
		{
			continue;
		}
		if (Widget->GetOwningPlayer() == PC)
		{
			Widget->NotifyLockedTargetChanged(Target);
		}
	}
}

void UBH_HUDWidget::PollStance()
{
	const UAbilitySystemComponent* ASC = BoundASC.Get();
	if (!ASC)
	{
		return;
	}
	const FString Stance = UBH_CombatFunctionLibrary::GetCurrentOverlayPoseDisplayName(ASC->GetAvatarActor());
	if (!Stance.IsEmpty() && !Stance.Equals(CurrentStanceName, ESearchCase::CaseSensitive))
	{
		BroadcastStanceChanged(ASC->GetAvatarActor(), Stance);
	}
}

void UBH_HUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!bIsNestedHUD && BoundASC.IsValid())
	{
		StancePollTimer += InDeltaTime;
		if (StancePollTimer >= StancePollInterval)
		{
			StancePollTimer = 0.f;
			PollStance();
		}
	}
}

void UBH_HUDWidget::UnbindFromASC()
{
	if (UAbilitySystemComponent* ASC = BoundASC.Get())
	{
		ASC->GetGameplayAttributeValueChangeDelegate(UAH_AttributeSet::GetHealthAttribute()).Remove(HealthHandle);
		ASC->GetGameplayAttributeValueChangeDelegate(UAH_AttributeSet::GetMaxHealthAttribute()).Remove(MaxHealthHandle);
		ASC->GetGameplayAttributeValueChangeDelegate(UAH_AttributeSet::GetStaminaAttribute()).Remove(StaminaHandle);
		ASC->GetGameplayAttributeValueChangeDelegate(UAH_AttributeSet::GetMaxStaminaAttribute()).Remove(MaxStaminaHandle);
		ASC->GetGameplayAttributeValueChangeDelegate(UAH_AttributeSet::GetPostureAttribute()).Remove(PostureHandle);
		ASC->GetGameplayAttributeValueChangeDelegate(UAH_AttributeSet::GetMaxPostureAttribute()).Remove(MaxPostureHandle);
		ASC->RegisterGameplayTagEvent(TAG_State_Combat_PostureBroken, EGameplayTagEventType::NewOrRemoved).Remove(PostureBrokenTagHandle);
	}
	HealthHandle.Reset();
	MaxHealthHandle.Reset();
	StaminaHandle.Reset();
	MaxStaminaHandle.Reset();
	PostureHandle.Reset();
	MaxPostureHandle.Reset();
	PostureBrokenTagHandle.Reset();
	BoundASC.Reset();
}

void UBH_HUDWidget::NativeDestruct()
{
	UnbindFromASC();
	Super::NativeDestruct();
}
