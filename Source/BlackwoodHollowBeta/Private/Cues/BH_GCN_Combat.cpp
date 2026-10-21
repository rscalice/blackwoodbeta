// Blackwood Hollow - Combat GameplayCue notifies (implementation)

#include "Cues/BH_GCN_Combat.h"
#include "Cues/BH_CueUtils.h"
#include "Combat/BH_HitFeedback.h"
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
#include "GameFramework/PlayerController.h"

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
	TierFXScales = { 0.85f, 1.f, 1.3f, 1.5f }; // Light, Medium, Heavy, Massive
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
	// Flesh FX (blood) follows the blow, into the victim. Blocked FX (sparks, local +X = spray axis) bounces back out of the blocker
	// toward the attacker, so it is oriented along -Normal.
	const FVector FXDirection = bBlocked ? -Normal : Normal;
	const FRotator ImpactRotation = FXDirection.IsNearlyZero() ? FRotator::ZeroRotator : FXDirection.Rotation();

	// Tiered feel (hit-stop + shake + flash) and vocals. Tier rules live in BH_CombatFeel.h.
	bool bBlockerStaminaZero = false;
	bool bFatal = false;
	if (const UAbilitySystemComponent* VictimASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Victim))
	{
		if (VictimASC->HasAttributeSetForAttribute(UAH_AttributeSet::GetStaminaAttribute()))
		{
			bBlockerStaminaZero = VictimASC->GetNumericAttribute(UAH_AttributeSet::GetStaminaAttribute()) <= 0.f;
		}
		// The server stamps Combat.HitResult.Fatal on the cue when the victim's Health was zero after the damage; the local
		// Dead tag / Health check stays as a fallback (older callers, or a cue fired without the tag).
		bFatal = !bBlocked && (Parameters.AggregatedSourceTags.HasTag(TAG_Combat_HitResult_Fatal)
			|| VictimASC->HasMatchingGameplayTag(TAG_State_Combat_Dead)
			|| (VictimASC->HasAttributeSetForAttribute(UAH_AttributeSet::GetHealthAttribute())
				&& VictimASC->GetNumericAttribute(UAH_AttributeSet::GetHealthAttribute()) <= 0.f));
	}
	EBH_ImpactTier Tier = bUseFixedTier ? FixedTier : UBH_CombatFeelLibrary::TierForHit(Parameters.RawMagnitude, Parameters.NormalizedMagnitude, bBlocked, bBlockerStaminaZero);
	if (bFatal && Tier < EBH_ImpactTier::Heavy)
	{
		Tier = EBH_ImpactTier::Heavy;
	}
	// Final combo step (server-stamped tag): at least Heavy, so the impact light, strong mesh flash and Heavy shake fire.
	Tier = UBH_CombatFeelLibrary::ApplyFinisherFloor(Tier, !bBlocked && Parameters.AggregatedSourceTags.HasTag(TAG_Combat_HitResult_Finisher));
	UBH_CombatFeelLibrary::PlayImpactFeel(Attacker ? Attacker : Victim, Tier, Attacker, Victim, ImpactLocation, /*bEscalateForVictim*/ !bBlocked, false, /*bBlocked*/ bBlocked);

	// 13F-1: the local HitFeedback event (surface, blight, outcome) for Blueprint sounds / sparks / shake / rumble; runs on every machine like this cue.
	UBH_HitFeedbackSubsystem::Report(Attacker ? Attacker : Victim, Attacker, Victim, Tier, bBlocked ? EBH_HitOutcome::Block : EBH_HitOutcome::Hit, bFatal, ImpactLocation, Normal);

	// Overhead enemy bar: a hit (blocked or not) by the LOCAL player shows the victim's bar for a few seconds (cosmetic, per machine).
	UBH_OverheadVitalsComponent::NotifyHitByLocalPlayer(Attacker, Victim);

	if (Victim && !bBlocked)
	{
		UBH_CombatFeelLibrary::PlayVoice(Victim, bFatal ? EBH_VoiceCategory::Death : (Tier >= EBH_ImpactTier::Heavy ? EBH_VoiceCategory::HurtHeavy : EBH_VoiceCategory::Hurt));
	}

	UE_LOG(LogBHCue, Verbose, TEXT("BH_GCN_CombatHit(%s): %s FX at %s"), *GetNameSafe(this), bBlocked ? TEXT("blocked") : TEXT("flesh"), *ImpactLocation.ToCompactString());
	// VFX every hit; the impact sound is rate-limited per victim (multi-hit spins would otherwise stack ~1.3 s clips).
	FBH_CueFX FX = bBlocked ? BlockedFX : FleshFX;
	if (TierFXScales.IsValidIndex(static_cast<int32>(Tier)))
	{
		FX.Scale *= TierFXScales[static_cast<int32>(Tier)]; // bigger blood / sparks for bigger hits
	}
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

	// 13F-1: the local HitFeedback event. The Victim is the defender who parried (a perfect block); the attacker's blow was turned aside.
	UBH_HitFeedbackSubsystem::Report(Parrier ? Parrier : Attacker, Attacker, Parrier, EBH_ImpactTier::Heavy, EBH_HitOutcome::PerfectBlock, false, Midpoint,
		(Parrier && Attacker) ? (Parrier->GetActorLocation() - Attacker->GetActorLocation()).GetSafeNormal() : FVector::ZeroVector);

	// Camera punch for the local parrier: strong short shake + FOV kick + rumble (no-op for everyone else).
	if (APlayerController* LocalPC = BH_CueUtils::FindLocalControllerInvolving(Parrier, nullptr))
	{
		UBH_CombatFeelLibrary::PlayParryPunch(LocalPC);
	}

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

	// 13F-1: the local HitFeedback event. The Victim is the actor whose posture broke; the Attacker is whoever broke it (may be null).
	UBH_HitFeedbackSubsystem::Report(MyTarget, Breaker, MyTarget, EBH_ImpactTier::Massive, EBH_HitOutcome::PostureBreak, false, SpawnLocation,
		Breaker ? (MyTarget->GetActorLocation() - Breaker->GetActorLocation()).GetSafeNormal() : FVector::ZeroVector);

	BH_CueUtils::PlayCueFX(MyTarget, PrimaryFX, SpawnLocation, FRotator::ZeroRotator, Attenuation);
	BH_CueUtils::PlayCueFX(MyTarget, SecondaryFX, SpawnLocation, FRotator::ZeroRotator, Attenuation);
	BH_CueUtils::PlayRandomSound(MyTarget, ShatterSounds, SpawnLocation, ShatterVolumeRange, ShatterPitchRange, Attenuation); // empty = silent
	return true;
}
