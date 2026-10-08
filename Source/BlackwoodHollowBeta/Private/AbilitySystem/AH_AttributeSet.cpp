// Blackwood Hollow - Core combat AttributeSet (implementation)

#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystem/Abilities/AH_GA_Block.h"
#include "AbilitySystem/Effects/AH_GE_CombatEffects.h"
#include "Combat/BH_CombatFeel.h"
#include "Progression/BH_RPGSettings.h"
#include "GameplayCueManager.h"
#include "GameplayEffectTypes.h"
#include "GameplayEffectExtension.h"
#include "Net/UnrealNetwork.h"
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"
#include "TimerManager.h"

static TAutoConsoleVariable<float> CVarBHPostureRegenDelay(
	TEXT("bh.Combat.PostureRegenDelay"),
	1.5f,
	TEXT("Seconds passive posture regeneration stays paused after taking posture damage (0 = no delay)."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarBHStaminaRegenDelay(
	TEXT("bh.Combat.StaminaRegenDelay"),
	0.9f,
	TEXT("Seconds passive stamina regeneration stays paused after any stamina spend (0 = no delay)."),
	ECVF_Default);

namespace BH_AttributeSetBlightPrivate
{
	/** Phase 8C: true for Blight DoT ticks (UAH_GE_BlightDoT or anything tagged Damage.Type.Blight). They bypass block mitigation and the DamageReceived reaction event. */
	static bool IsBlightSpec(const FGameplayEffectSpec& Spec)
	{
		if (Spec.Def && Spec.Def->IsA(UAH_GE_BlightDoT::StaticClass()))
		{
			return true;
		}
		FGameplayTagContainer SpecAssetTags;
		Spec.GetAllAssetTags(SpecAssetTags);
		return SpecAssetTags.HasTag(TAG_Damage_Type_Blight);
	}
}

UAH_AttributeSet::UAH_AttributeSet()
{
	InitHealth(100.f);
	InitMaxHealth(100.f);
	InitPosture(100.f);
	InitMaxPosture(100.f);
	InitPostureRegenRate(5.f);
	InitStamina(100.f);
	InitMaxStamina(100.f);
	InitStaminaRegenRate(22.f);
	InitAttackPower(10.f);
	InitDefense(5.f);
	InitAttackSpeed(1.f);
	InitBlightResistance(0.f);
	InitLevel(1.f);
	InitIncomingDamage(0.f);
}

void UAH_AttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION_NOTIFY(UAH_AttributeSet, Health, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAH_AttributeSet, MaxHealth, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAH_AttributeSet, Posture, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAH_AttributeSet, MaxPosture, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAH_AttributeSet, PostureRegenRate, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAH_AttributeSet, Stamina, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAH_AttributeSet, MaxStamina, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAH_AttributeSet, StaminaRegenRate, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAH_AttributeSet, AttackPower, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAH_AttributeSet, Defense, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAH_AttributeSet, AttackSpeed, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAH_AttributeSet, BlightResistance, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UAH_AttributeSet, Level, COND_None, REPNOTIFY_Always);
}

void UAH_AttributeSet::ClampAttribute(const FGameplayAttribute& Attribute, float& NewValue) const
{
	if (Attribute == GetHealthAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxHealth());
	}
	else if (Attribute == GetPostureAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxPosture());
	}
	else if (Attribute == GetStaminaAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxStamina());
	}
	else if (Attribute == GetMaxHealthAttribute() || Attribute == GetMaxPostureAttribute()
		|| Attribute == GetMaxStaminaAttribute())
	{
		NewValue = FMath::Max(NewValue, 1.f);
	}
	else if (Attribute == GetPostureRegenRateAttribute() || Attribute == GetStaminaRegenRateAttribute())
	{
		NewValue = FMath::Max(NewValue, 0.f);
	}
	else if (Attribute == GetBlightResistanceAttribute())
	{
		// Percentage-style mitigation, clamp to [0, 0.9] so Blight damage is never fully negated.
		NewValue = FMath::Clamp(NewValue, 0.f, 0.9f);
	}
	else if (Attribute == GetAttackSpeedAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.5f, 2.0f);
	}
	else if (Attribute == GetLevelAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 1.f, static_cast<float>(FMath::Max(UBH_RPGSettings::GetMaxLevel(), 1)));
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

void UAH_AttributeSet::PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue)
{
	Super::PostAttributeChange(Attribute, OldValue, NewValue);

	// ClampAttribute only runs when the CURRENT value changes; a shrinking Max would otherwise leave
	// Health/Posture/Stamina above their new cap. Server only: clients receive the clamped base via replication.
	if (NewValue >= OldValue)
	{
		return;
	}
	UAbilitySystemComponent* ASC = GetOwningAbilitySystemComponent();
	if (!ASC || !ASC->IsOwnerActorAuthoritative())
	{
		return;
	}

	FGameplayAttribute Current;
	float CurrentValue = 0.f;
	if (Attribute == GetMaxHealthAttribute())
	{
		Current = GetHealthAttribute();
		CurrentValue = GetHealth();
	}
	else if (Attribute == GetMaxPostureAttribute())
	{
		Current = GetPostureAttribute();
		CurrentValue = GetPosture();
	}
	else if (Attribute == GetMaxStaminaAttribute())
	{
		Current = GetStaminaAttribute();
		CurrentValue = GetStamina();
	}
	else
	{
		return;
	}

	if (CurrentValue > NewValue)
	{
		ASC->SetNumericAttributeBase(Current, NewValue);
	}
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

	// -- Dodge i-frames -----------------------------------------------------
	// Melee damage and melee posture damage are thrown out while the target has
	// State.Combat.Invulnerable (granted by UAH_GA_Dodge during its i-frame window).
	// UANS_MeleeHitbox already skips invulnerable victims before sending any event;
	// this is the authoritative backstop (e.g. a hit that landed one tick before the window opened).
	if (Data.Target.HasMatchingGameplayTag(TAG_State_Combat_Invulnerable))
	{
		const bool bIncomingDamage = Data.EvaluatedData.Attribute == GetIncomingDamageAttribute();
		const bool bPostureDamage = Data.EvaluatedData.Attribute == GetPostureAttribute() && Data.EvaluatedData.Magnitude < 0.f;
		if (bIncomingDamage || bPostureDamage)
		{
			FGameplayTagContainer SpecAssetTags;
			Data.EffectSpec.GetAllAssetTags(SpecAssetTags);
			if (SpecAssetTags.HasTag(FBH_GameplayTags::Get().Damage_Type_Melee))
			{
				return false;
			}
		}
	}

	// -- Block mitigation ---------------------------------------------------
	// While UAH_GA_Block is active (State.Combat.Blocking) and the attacker is
	// inside its frontal arc, cut the damage and convert part of it into
	// Posture loss. Tunables live on the active block ability instance.
	if (Data.EvaluatedData.Attribute == GetIncomingDamageAttribute()
		&& Data.EvaluatedData.Magnitude > 0.f
		&& Data.Target.HasMatchingGameplayTag(FBH_GameplayTags::Get().State_Combat_Blocking)
		&& !BH_AttributeSetBlightPrivate::IsBlightSpec(Data.EffectSpec))
	{
		if (const UAH_GA_Block* Block = UAH_GA_Block::FindActiveBlock(&Data.Target))
		{
			const AActor* Attacker = Data.EffectSpec.GetContext().GetOriginalInstigator();
			if (Block->IsAttackInBlockArc(Attacker))
			{
				const float Incoming = Data.EvaluatedData.Magnitude;
				Data.EvaluatedData.Magnitude = Incoming * (1.f - Block->DamageReduction);
				bPendingBlockedHit = true;
				PendingBlockPostureCost = Incoming * Block->PostureDamageScale;
			}
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

		const bool bWasBlocked = bPendingBlockedHit;
		const float BlockPostureCost = PendingBlockPostureCost;
		bPendingBlockedHit = false;
		PendingBlockPostureCost = 0.f;

		float NewHealth = GetHealth();
		if (DamageDone > 0.f)
		{
			NewHealth = FMath::Clamp(GetHealth() - DamageDone, 0.f, GetMaxHealth());
			SetHealth(NewHealth);
		}

		if (bWasBlocked)
		{
			// Blocked: posture takes the hit, the blocker gets a block-impact event
			// (UAH_GA_Block plays its Impact section) instead of a hit reaction.
			if (BlockPostureCost > 0.f)
			{
				const float NewPosture = FMath::Clamp(GetPosture() - BlockPostureCost, 0.f, GetMaxPosture());
				SetPosture(NewPosture);
				StartPostureRegenDelay(TargetASC);
				if (NewPosture <= 0.f)
				{
					HandlePostureDepleted(TargetASC, Instigator, TargetActor);
				}
			}

			if (TargetASC)
			{
				FGameplayEventData BlockEvent;
				BlockEvent.EventTag = Tags.Event_Combat_BlockImpact;
				BlockEvent.Instigator = Instigator;
				BlockEvent.Target = TargetActor;
				BlockEvent.EventMagnitude = BlockPostureCost;
				TargetASC->HandleGameplayEvent(Tags.Event_Combat_BlockImpact, &BlockEvent);
			}
		}
		else if (DamageDone > 0.f && TargetASC && BH_AttributeSetBlightPrivate::IsBlightSpec(Data.EffectSpec))
		{
			// Blight DoT tick: Health only. No DamageReceived (that drives UAH_GA_HitReaction / flash), no posture, no cue.
			FGameplayEventData BlightEvent;
			BlightEvent.EventTag = Tags.Event_Combat_BlightDamage;
			BlightEvent.Instigator = Instigator;
			BlightEvent.Target = TargetActor;
			BlightEvent.EventMagnitude = DamageDone;
			TargetASC->HandleGameplayEvent(Tags.Event_Combat_BlightDamage, &BlightEvent);
		}
		else if (DamageDone > 0.f && TargetASC)
		{
			// Post-damage notification (UAH_GA_HitReaction, UI). Event.Combat.Hit is
			// reserved for the PRE-damage melee hit sent by UANS_MeleeHitbox.
			FGameplayEventData EventData;
			EventData.EventTag = Tags.Event_Combat_DamageReceived;
			EventData.Instigator = Instigator;
			EventData.Target = TargetActor;
			EventData.EventMagnitude = DamageDone;
			TargetASC->HandleGameplayEvent(Tags.Event_Combat_DamageReceived, &EventData);
		}

		if (DamageDone > 0.f && NewHealth <= 0.f && TargetASC && !TargetASC->HasMatchingGameplayTag(Tags.State_Combat_Dead))
		{
			OnHealthZero.Broadcast(Instigator);
			// Replicated loose tag (TagOnly): UpdateTagMap also bumps the authority's own count, so the server sees it too.
			TargetASC->AddLooseGameplayTag(Tags.State_Combat_Dead, 1, EGameplayTagReplicationState::TagOnly);

			// Death vocal (cosmetic, this machine). Melee kills are also voiced by the hit cue on every machine; PlayVoice's
			// 2 s per-actor death limit keeps that from doubling here. Covers non-melee deaths (blight, effects) on the host.
			UBH_CombatFeelLibrary::PlayVoice(TargetActor, EBH_VoiceCategory::Death);

			FGameplayEventData DeathEvent;
			DeathEvent.EventTag = Tags.Event_Combat_Death;
			DeathEvent.Instigator = Instigator;
			DeathEvent.Target = TargetActor;
			TargetASC->HandleGameplayEvent(Tags.Event_Combat_Death, &DeathEvent);
		}
	}
	// -- Health/Posture can also be modified directly by effects -----
	else if (Data.EvaluatedData.Attribute == GetHealthAttribute())
	{
		const float NewHealth = FMath::Clamp(GetHealth(), 0.f, GetMaxHealth());
		SetHealth(NewHealth);

		if (NewHealth <= 0.f)
		{
			OnHealthZero.Broadcast(Instigator);
			if (TargetASC)
			{
				TargetASC->AddLooseGameplayTag(Tags.State_Combat_Dead, 1, EGameplayTagReplicationState::TagOnly);
			}
		}
	}
	else if (Data.EvaluatedData.Attribute == GetPostureAttribute())
	{
		const float NewPosture = FMath::Clamp(GetPosture(), 0.f, GetMaxPosture());
		SetPosture(NewPosture);

		// Posture damage (negative delta, e.g. UAH_GE_PostureDamage / parry costs) holds off
		// passive regen for a moment. Regen ticks themselves are positive and don't.
		if (Data.EvaluatedData.Magnitude < 0.f)
		{
			StartPostureRegenDelay(TargetASC);
		}

		if (NewPosture <= 0.f)
		{
			HandlePostureDepleted(TargetASC, Instigator, TargetActor);
		}
	}
	else if (Data.EvaluatedData.Attribute == GetStaminaAttribute())
	{
		SetStamina(FMath::Clamp(GetStamina(), 0.f, GetMaxStamina()));

		// Any spend (negative delta) holds off passive regen for a moment; regen ticks are positive and don't.
		if (Data.EvaluatedData.Magnitude < 0.f)
		{
			StartStaminaRegenDelay(TargetASC);
		}
	}
}

void UAH_AttributeSet::StartPostureRegenDelay(UAbilitySystemComponent* TargetASC)
{
	const float Delay = CVarBHPostureRegenDelay.GetValueOnGameThread();
	UWorld* World = TargetASC ? TargetASC->GetWorld() : nullptr;
	if (!World || Delay <= 0.f)
	{
		return;
	}

	// Server-only loose tag: UAH_GE_PostureRegen's ongoing tag requirements ignore it,
	// which inhibits the periodic regen until the timer clears it. Re-hits restart the timer.
	TargetASC->SetLooseGameplayTagCount(TAG_State_Combat_PostureRegenDelayed, 1);

	TWeakObjectPtr<UAbilitySystemComponent> WeakASC(TargetASC);
	World->GetTimerManager().SetTimer(PostureRegenDelayTimer, FTimerDelegate::CreateWeakLambda(this, [WeakASC]()
	{
		if (UAbilitySystemComponent* ASC = WeakASC.Get())
		{
			ASC->SetLooseGameplayTagCount(TAG_State_Combat_PostureRegenDelayed, 0);
		}
	}), Delay, false);
}

void UAH_AttributeSet::StartStaminaRegenDelay(UAbilitySystemComponent* TargetASC)
{
	const float Delay = CVarBHStaminaRegenDelay.GetValueOnGameThread();
	UWorld* World = TargetASC ? TargetASC->GetWorld() : nullptr;
	if (!World || Delay <= 0.f)
	{
		return;
	}

	// Loose tag, same pattern as the posture delay: UAH_GE_StaminaRegen is inhibited while it is present; re-spending restarts the timer.
	TargetASC->SetLooseGameplayTagCount(TAG_State_Combat_StaminaRegenDelayed, 1);

	TWeakObjectPtr<UAbilitySystemComponent> WeakASC(TargetASC);
	World->GetTimerManager().SetTimer(StaminaRegenDelayTimer, FTimerDelegate::CreateWeakLambda(this, [WeakASC]()
	{
		if (UAbilitySystemComponent* ASC = WeakASC.Get())
		{
			ASC->SetLooseGameplayTagCount(TAG_State_Combat_StaminaRegenDelayed, 0);
		}
	}), Delay, false);
}

void UAH_AttributeSet::HandlePostureDepleted(UAbilitySystemComponent* TargetASC, AActor* Instigator, AActor* TargetActor)
{
	const FBH_GameplayTags& Tags = FBH_GameplayTags::Get();
	if (!TargetASC || TargetASC->HasMatchingGameplayTag(Tags.State_Combat_PostureBroken))
	{
		return;
	}

	// Replicated loose tag (TagOnly) so every machine sees the broken state; UAH_GA_PostureBreak clears it on the server.
	TargetASC->AddLooseGameplayTag(Tags.State_Combat_PostureBroken, 1, EGameplayTagReplicationState::TagOnly);
	OnPostureBroken.Broadcast(Instigator);

	// Cosmetic cue (replicated): shatter VFX/SFX on every machine. Server-side only (this runs from GE execution).
	{
		FGameplayCueParameters CueParams;
		CueParams.Instigator = Instigator;
		CueParams.EffectCauser = Instigator;
		CueParams.SourceObject = TargetActor;
		if (TargetActor)
		{
			CueParams.Location = TargetActor->GetActorLocation();
			CueParams.TargetAttachComponent = TargetActor->GetRootComponent();
		}
		TargetASC->ExecuteGameplayCue(TAG_GameplayCue_Combat_PostureBroken, CueParams);
	}

	// Triggers UAH_GA_PostureBreak (which owns the tag from here on).
	FGameplayEventData EventData;
	EventData.EventTag = Tags.Event_Combat_PostureBreak;
	EventData.Instigator = Instigator;
	EventData.Target = TargetActor;
	TargetASC->HandleGameplayEvent(Tags.Event_Combat_PostureBreak, &EventData);
}

void UAH_AttributeSet::OnRep_Health(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAH_AttributeSet, Health, OldValue);
}

void UAH_AttributeSet::OnRep_MaxHealth(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAH_AttributeSet, MaxHealth, OldValue);
}

void UAH_AttributeSet::OnRep_Posture(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAH_AttributeSet, Posture, OldValue);
}

void UAH_AttributeSet::OnRep_MaxPosture(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAH_AttributeSet, MaxPosture, OldValue);
}

void UAH_AttributeSet::OnRep_PostureRegenRate(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAH_AttributeSet, PostureRegenRate, OldValue);
}

void UAH_AttributeSet::OnRep_Stamina(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAH_AttributeSet, Stamina, OldValue);
}

void UAH_AttributeSet::OnRep_MaxStamina(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAH_AttributeSet, MaxStamina, OldValue);
}

void UAH_AttributeSet::OnRep_StaminaRegenRate(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAH_AttributeSet, StaminaRegenRate, OldValue);
}

void UAH_AttributeSet::OnRep_AttackPower(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAH_AttributeSet, AttackPower, OldValue);
}

void UAH_AttributeSet::OnRep_Defense(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAH_AttributeSet, Defense, OldValue);
}

void UAH_AttributeSet::OnRep_AttackSpeed(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAH_AttributeSet, AttackSpeed, OldValue);
}

void UAH_AttributeSet::OnRep_BlightResistance(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAH_AttributeSet, BlightResistance, OldValue);
}

void UAH_AttributeSet::OnRep_Level(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UAH_AttributeSet, Level, OldValue);
}
