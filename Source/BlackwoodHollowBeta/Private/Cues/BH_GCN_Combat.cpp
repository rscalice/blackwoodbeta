// Blackwood Hollow - Combat GameplayCue notifies (implementation)

#include "Cues/BH_GCN_Combat.h"
#include "Cues/BH_CueUtils.h"
#include "UI/BH_OverheadVitalsComponent.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "GameplayCueManager.h"
#include "NiagaraSystem.h"
#include "Sound/SoundAttenuation.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace
{
	/** Last impact-sound time per victim (shared by every hit cue CDO; cosmetic, game-thread only). */
	TMap<TWeakObjectPtr<const AActor>, double> GLastHitSoundTime;

	/** Returns true (and records the time) when a hit sound may play on Victim; false while rate-limited. */
	bool ConsumeHitSoundSlot(const AActor* Victim, const UWorld* World, float MinInterval)
	{
		if (MinInterval <= 0.f || !Victim || !World)
		{
			return true;
		}

		const double Now = World->GetTimeSeconds();

		// Occasional prune of dead victims and stale entries.
		if (GLastHitSoundTime.Num() > 32)
		{
			for (auto It = GLastHitSoundTime.CreateIterator(); It; ++It)
			{
				if (!It.Key().IsValid() || Now - It.Value() > 5.0 || It.Value() > Now)
				{
					It.RemoveCurrent();
				}
			}
		}

		double& Last = GLastHitSoundTime.FindOrAdd(Victim, -1.0e9);
		if (Now < Last || Now - Last >= MinInterval) // Now < Last: the world time reset (new PIE session)
		{
			Last = Now;
			return true;
		}
		return false;
	}
}

// ============================================================================
// Hit
// ============================================================================

UBH_GCN_CombatHit::UBH_GCN_CombatHit()
{
	GameplayCueTag = TAG_GameplayCue_Combat_Hit;
}

bool UBH_GCN_CombatHit::OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const
{
	AActor* Attacker = MyTarget;
	AActor* Victim = const_cast<AActor*>(Cast<AActor>(Parameters.SourceObject.Get()));
	if (!Attacker && !Victim)
	{
		return false;
	}

	UE_LOG(LogBHCue, Log, TEXT("BH_GCN_CombatHit: attacker=%s victim=%s dmg=%.1f"),
		*GetNameSafe(Attacker), *GetNameSafe(Victim), Parameters.RawMagnitude);

	// Impact FX: blade impact point if the ability supplied one, otherwise chest height on the victim.
	const bool bBlocked = Parameters.AggregatedSourceTags.HasTag(TAG_Combat_HitResult_Blocked);
	FVector ImpactLocation = FVector(Parameters.Location);
	if (ImpactLocation.IsZero())
	{
		const AActor* Anchor = Victim ? Victim : Attacker;
		ImpactLocation = Anchor->GetActorLocation() + FallbackVictimOffset;
	}
	const FVector Normal = FVector(Parameters.Normal);
	const FRotator ImpactRotation = Normal.IsNearlyZero() ? FRotator::ZeroRotator : Normal.Rotation();

	// Tiered feel (hit-stop + shake + flash) and vocals. Tier rules live in BH_CombatFeel.h.
	bool bBlockerStaminaZero = false;
	bool bFatal = false;
	if (const UAbilitySystemComponent* VictimASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Victim))
	{
		if (VictimASC->HasAttributeSetForAttribute(UAH_AttributeSet::GetStaminaAttribute()))
		{
			bBlockerStaminaZero = VictimASC->GetNumericAttribute(UAH_AttributeSet::GetStaminaAttribute()) <= 0.f;
			bFatal = !bBlocked && (VictimASC->HasMatchingGameplayTag(TAG_State_Combat_Dead)
				|| VictimASC->GetNumericAttribute(UAH_AttributeSet::GetHealthAttribute()) <= 0.f);
		}
	}
	EBH_ImpactTier Tier = bUseFixedTier ? FixedTier : UBH_CombatFeelLibrary::TierForHit(Parameters.RawMagnitude, Parameters.NormalizedMagnitude, bBlocked, bBlockerStaminaZero);
	if (bFatal && Tier < EBH_ImpactTier::Heavy)
	{
		Tier = EBH_ImpactTier::Heavy;
	}
	UBH_CombatFeelLibrary::PlayImpactFeel(Attacker ? Attacker : Victim, Tier, Attacker, Victim, ImpactLocation, /*bEscalateForVictim*/ !bBlocked, false, /*bBlocked*/ bBlocked);

	// Overhead enemy bar: a hit (blocked or not) by the LOCAL player shows the victim's bar for a few seconds (cosmetic, per machine).
	UBH_OverheadVitalsComponent::NotifyHitByLocalPlayer(Attacker, Victim);

	if (Victim && !bBlocked)
	{
		UBH_CombatFeelLibrary::PlayVoice(Victim, bFatal ? EBH_VoiceCategory::Death : (Tier >= EBH_ImpactTier::Heavy ? EBH_VoiceCategory::HurtHeavy : EBH_VoiceCategory::Hurt));
	}

	UE_LOG(LogBHCue, Verbose, TEXT("BH_GCN_CombatHit(%s): %s FX at %s"), *GetNameSafe(this), bBlocked ? TEXT("blocked") : TEXT("flesh"), *ImpactLocation.ToCompactString());
	// VFX every hit; the impact sound is rate-limited per victim (multi-hit spins would otherwise stack ~1.3 s clips).
	FBH_CueFX FX = bBlocked ? BlockedFX : FleshFX;
	const AActor* SoundKey = Victim ? Victim : Attacker;
	if (!ConsumeHitSoundSlot(SoundKey, SoundKey ? SoundKey->GetWorld() : nullptr, MinSoundInterval))
	{
		FX.Sounds.Reset();
	}
	BH_CueUtils::PlayCueFX(Attacker ? Attacker : Victim, FX, ImpactLocation, ImpactRotation, Attenuation);
	return true;
}

// ============================================================================
// Parry success
// ============================================================================

UBH_GCN_ParrySuccess::UBH_GCN_ParrySuccess()
{
	GameplayCueTag = TAG_GameplayCue_Combat_ParrySuccess;
}

bool UBH_GCN_ParrySuccess::OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const
{
	AActor* Parrier = MyTarget;
	AActor* Attacker = const_cast<AActor*>(Cast<AActor>(Parameters.SourceObject.Get()));
	if (!Attacker)
	{
		Attacker = Parameters.EffectCauser.Get();
	}
	if (!Attacker)
	{
		Attacker = Parameters.Instigator.Get();
	}
	if (!Parrier && !Attacker)
	{
		return false;
	}

	UE_LOG(LogBHCue, Log, TEXT("BH_GCN_ParrySuccess: parrier=%s attacker=%s"), *GetNameSafe(Parrier), *GetNameSafe(Attacker));

	// Clash FX at the midpoint between the two fighters.
	const FVector ParrierLocation = Parrier ? Parrier->GetActorLocation() : Attacker->GetActorLocation();
	const FVector AttackerLocation = Attacker ? Attacker->GetActorLocation() : ParrierLocation;
	const FVector Midpoint = (ParrierLocation + AttackerLocation) * 0.5 + FVector(0.0, 0.0, ParryFXHeight);
	const FVector Normal = FVector(Parameters.Normal);

	// Heavy feel: camera punch only for the local parrier (a local player who merely got parried gets no big shake).
	// The animation freeze is the attacker's alone (the parrier keeps moving so the counter feels responsive): bSkipFreeze + an explicit attacker freeze.
	UBH_CombatFeelLibrary::PlayImpactFeel(Parrier ? Parrier : Attacker, EBH_ImpactTier::Heavy, Parrier, Attacker, Midpoint, false, /*bInstigatorOnlyShake*/ true, false, /*bSkipFreeze*/ true);
	UBH_CombatFeelLibrary::ApplyTierFreeze(Attacker, UBH_CombatFeelSettings::Get()->ParryFreezeTier, 1.f, false);

	BH_CueUtils::PlayCueFX(Parrier ? Parrier : Attacker, ParryFX, Midpoint, Normal.IsNearlyZero() ? FRotator::ZeroRotator : Normal.Rotation(), Attenuation);
	return true;
}

// ============================================================================
// Posture broken
// ============================================================================

UBH_GCN_PostureBroken::UBH_GCN_PostureBroken()
{
	GameplayCueTag = TAG_GameplayCue_Combat_PostureBroken;
}

bool UBH_GCN_PostureBroken::OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const
{
	UWorld* World = MyTarget ? MyTarget->GetWorld() : nullptr;
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return false;
	}

	const FVector Base = Parameters.Location.IsZero() ? MyTarget->GetActorLocation() : FVector(Parameters.Location);
	const FVector SpawnLocation = Base + SpawnOffset;

	UE_LOG(LogBHCue, Log, TEXT("BH_GCN_PostureBroken: target=%s primary=%s secondary=%s"),
		*GetNameSafe(MyTarget), *GetNameSafe(PrimaryFX.System), *GetNameSafe(SecondaryFX.System));

	AActor* Breaker = const_cast<AActor*>(Cast<AActor>(Parameters.Instigator.Get()));
	UBH_CombatFeelLibrary::PlayImpactFeel(MyTarget, EBH_ImpactTier::Massive, Breaker, MyTarget, SpawnLocation);
	UBH_CombatFeelLibrary::PlayVoice(MyTarget, EBH_VoiceCategory::PostureBreak);

	BH_CueUtils::PlayCueFX(MyTarget, PrimaryFX, SpawnLocation, FRotator::ZeroRotator, Attenuation);
	BH_CueUtils::PlayCueFX(MyTarget, SecondaryFX, SpawnLocation, FRotator::ZeroRotator, Attenuation);
	return true;
}
