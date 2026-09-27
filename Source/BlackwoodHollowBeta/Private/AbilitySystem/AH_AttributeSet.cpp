// Blackwood Hollow - Core combat AttributeSet (implementation)

#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "GameplayEffectExtension.h"
#include "Net/UnrealNetwork.h"

UAH_AttributeSet::UAH_AttributeSet()
{
	InitHealth(100.f);
	InitMaxHealth(100.f);
	InitMana(50.f);
	InitMaxMana(50.f);
	InitPosture(100.f);
	InitMaxPosture(100.f);
	InitAttackPower(10.f);
	InitDefense(5.f);
	InitBlightResistance(0.f);
	InitIncomingDamage(0.f);
}

void UAH_AttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION_NOTIFY(UAH_AttributeSet, Health, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAH_AttributeSet, MaxHealth, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAH_AttributeSet, Mana, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAH_AttributeSet, MaxMana, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAH_AttributeSet, Posture, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAH_AttributeSet, MaxPosture, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAH_AttributeSet, AttackPower, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAH_AttributeSet, Defense, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAH_AttributeSet, BlightResistance, COND_None, REPNOTIFY_Always);
}

void UAH_AttributeSet::ClampAttribute(const FGameplayAttribute& Attribute, float& NewValue) const
{
	if (Attribute == GetHealthAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxHealth());
	}
	else if (Attribute == GetManaAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxMana());
	}
	else if (Attribute == GetPostureAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxPosture());
	}
	else if (Attribute == GetMaxHealthAttribute() || Attribute == GetMaxManaAttribute() || Attribute == GetMaxPostureAttribute())
	{
		NewValue = FMath::Max(NewValue, 1.f);
	}
	else if (Attribute == GetBlightResistanceAttribute())
	{
		// Percentage-style mitigation, clamp to [0, 0.9] so Blight damage is never fully negated.
		NewValue = FMath::Clamp(NewValue, 0.f, 0.9f);
	}
	else if (Attribute == GetDefenseAttribute() || Attribute == GetAttackPowerAttribute())
	{
		NewValue = FMath::Max(NewValue, 0.f);
	}
}

void UAH_AttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);
	ClampAttribute(Attribute, NewValue);
}

void UAH_AttributeSet::PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const
{
	Super::PreAttributeBaseChange(Attribute, NewValue);
	ClampAttribute(Attribute, NewValue);
}

bool UAH_AttributeSet::PreGameplayEffectExecute(FGameplayEffectModCallbackData& Data)
{
	if (!Super::PreGameplayEffectExecute(Data))
	{
		return false;
	}

	// -- Parry negation -----------------------------------------------------
	// Melee damage (specs carrying Damage.Type.Melee, added by
	// UAH_GA_MeleeAttack_Base) is thrown out entirely while the target is inside
	// an active parry window (State.Combat.Parrying, granted by UAH_GA_Parry).
	// UAH_GA_Parry handles the posture side of the exchange from its
	// Event.Combat.Hit listener; this is the authoritative backstop that
	// guarantees no Health is lost regardless of event ordering.
	if (Data.EvaluatedData.Attribute == GetIncomingDamageAttribute()
		&& Data.Target.HasMatchingGameplayTag(FBH_GameplayTags::Get().State_Combat_Parrying))
	{
		FGameplayTagContainer SpecAssetTags;
		Data.EffectSpec.GetAllAssetTags(SpecAssetTags);
		if (SpecAssetTags.HasTag(FBH_GameplayTags::Get().Damage_Type_Melee))
		{
			return false;
		}
	}

	return true;
}

void UAH_AttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);

	const FBH_GameplayTags& Tags = FBH_GameplayTags::Get();
	FGameplayEffectContextHandle Context = Data.EffectSpec.GetContext();
	AActor* Instigator = Context.GetOriginalInstigator();
	AActor* TargetActor = Data.Target.GetAvatarActor();

	UAbilitySystemComponent* TargetASC = &Data.Target;

	// -- Route the IncomingDamage meta attribute into Health --------------
	if (Data.EvaluatedData.Attribute == GetIncomingDamageAttribute())
	{
		const float DamageDone = GetIncomingDamage();
		SetIncomingDamage(0.f);

		if (DamageDone > 0.f)
		{
			const float NewHealth = FMath::Clamp(GetHealth() - DamageDone, 0.f, GetMaxHealth());
			SetHealth(NewHealth);

			if (TargetASC)
			{
				// Post-damage notification (hit reactions, UI). Event.Combat.Hit is
				// reserved for the PRE-damage melee hit sent by UANS_MeleeHitbox.
				FGameplayEventData EventData;
				EventData.EventTag = Tags.Event_Combat_DamageReceived;
				EventData.Instigator = Instigator;
				EventData.Target = TargetActor;
				EventData.EventMagnitude = DamageDone;
				TargetASC->HandleGameplayEvent(Tags.Event_Combat_DamageReceived, &EventData);
			}

			if (NewHealth <= 0.f)
			{
				OnHealthZero.Broadcast(Instigator);

				if (TargetASC)
				{
					TargetASC->AddLooseGameplayTag(Tags.State_Combat_Dead);

					FGameplayEventData DeathEvent;
					DeathEvent.EventTag = Tags.Event_Combat_Death;
					DeathEvent.Instigator = Instigator;
					DeathEvent.Target = TargetActor;
					TargetASC->HandleGameplayEvent(Tags.Event_Combat_Death, &DeathEvent);
				}
			}
		}
	}
	// -- Health/Posture/Mana can also be modified directly by effects -----
	else if (Data.EvaluatedData.Attribute == GetHealthAttribute())
	{
		const float NewHealth = FMath::Clamp(GetHealth(), 0.f, GetMaxHealth());
		SetHealth(NewHealth);

		if (NewHealth <= 0.f)
		{
			OnHealthZero.Broadcast(Instigator);
			if (TargetASC)
			{
				TargetASC->AddLooseGameplayTag(Tags.State_Combat_Dead);
			}
		}
	}
	else if (Data.EvaluatedData.Attribute == GetPostureAttribute())
	{
		const float NewPosture = FMath::Clamp(GetPosture(), 0.f, GetMaxPosture());
		SetPosture(NewPosture);

		if (NewPosture <= 0.f && TargetASC && !TargetASC->HasMatchingGameplayTag(Tags.State_Combat_PostureBroken))
		{
			TargetASC->AddLooseGameplayTag(Tags.State_Combat_PostureBroken);
			OnPostureBroken.Broadcast(Instigator);

			FGameplayEventData EventData;
			EventData.EventTag = Tags.Event_Combat_PostureBreak;
			EventData.Instigator = Instigator;
			EventData.Target = TargetActor;
			TargetASC->HandleGameplayEvent(Tags.Event_Combat_PostureBreak, &EventData);
		}
	}
	else if (Data.EvaluatedData.Attribute == GetManaAttribute())
	{
		SetMana(FMath::Clamp(GetMana(), 0.f, GetMaxMana()));
	}
}

void UAH_AttributeSet::OnRep_Health(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAH_AttributeSet, Health, OldValue);
}

void UAH_AttributeSet::OnRep_MaxHealth(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAH_AttributeSet, MaxHealth, OldValue);
}

void UAH_AttributeSet::OnRep_Mana(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAH_AttributeSet, Mana, OldValue);
}

void UAH_AttributeSet::OnRep_MaxMana(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAH_AttributeSet, MaxMana, OldValue);
}

void UAH_AttributeSet::OnRep_Posture(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAH_AttributeSet, Posture, OldValue);
}

void UAH_AttributeSet::OnRep_MaxPosture(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAH_AttributeSet, MaxPosture, OldValue);
}

void UAH_AttributeSet::OnRep_AttackPower(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAH_AttributeSet, AttackPower, OldValue);
}

void UAH_AttributeSet::OnRep_Defense(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAH_AttributeSet, Defense, OldValue);
}

void UAH_AttributeSet::OnRep_BlightResistance(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAH_AttributeSet, BlightResistance, OldValue);
}
