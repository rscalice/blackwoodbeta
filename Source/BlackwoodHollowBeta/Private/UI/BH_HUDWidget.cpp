// Blackwood Hollow - HUD widget base (implementation)

#include "UI/BH_HUDWidget.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystem/BH_CombatFunctionLibrary.h"
#include "AbilitySystemComponent.h"
#include "Blueprint/WidgetTree.h"
#include "UObject/UObjectIterator.h"

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
				Child->InitializeHUD(ASC);
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
