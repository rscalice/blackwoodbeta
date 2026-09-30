// Blackwood Hollow - Combat GameplayCue notifies (implementation)

#include "Cues/BH_GCN_Combat.h"
#include "Cues/BH_CameraShakes.h"
#include "Cues/BH_CueUtils.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "GameplayCueManager.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

// ============================================================================
// Hit
// ============================================================================

UBH_GCN_CombatHit::UBH_GCN_CombatHit()
{
	GameplayCueTag = TAG_GameplayCue_Combat_Hit;
	HitShakeClass = UBH_CameraShake_Hit::StaticClass();
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

	if (HitStopDuration > 0.f)
	{
		BH_CueUtils::ApplyHitStop(Attacker, HitStopDuration);
		if (Victim != Attacker)
		{
			BH_CueUtils::ApplyHitStop(Victim, HitStopDuration);
		}
	}

	if (UCameraShakeBase* Shake = BH_CueUtils::PlayDirectionalShake(Attacker, Victim, HitShakeClass, HitShakeScale, FVector(Parameters.Normal)))
	{
		UE_LOG(LogBHCue, Log, TEXT("BH_GCN_CombatHit: camera shake %s started"), *GetNameSafe(Shake));
	}
	return true;
}

// ============================================================================
// Parry success
// ============================================================================

UBH_GCN_ParrySuccess::UBH_GCN_ParrySuccess()
{
	GameplayCueTag = TAG_GameplayCue_Combat_ParrySuccess;
	ParryShakeClass = UBH_CameraShake_ParryPunch::StaticClass();
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

	if (HitStopDuration > 0.f)
	{
		BH_CueUtils::ApplyHitStop(Parrier, HitStopDuration);
		if (Attacker != Parrier)
		{
			BH_CueUtils::ApplyHitStop(Attacker, HitStopDuration);
		}
	}

	// Punch only for the local parrier (a local player who merely got parried does not get the big shake).
	if (Parrier)
	{
		if (UCameraShakeBase* Shake = BH_CueUtils::PlayDirectionalShake(Parrier, nullptr, ParryShakeClass, ParryShakeScale, FVector(Parameters.Normal)))
		{
			UE_LOG(LogBHCue, Log, TEXT("BH_GCN_ParrySuccess: camera shake %s started"), *GetNameSafe(Shake));
		}
	}
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

	UE_LOG(LogBHCue, Log, TEXT("BH_GCN_PostureBroken: target=%s system=%s sound=%s"),
		*GetNameSafe(MyTarget), *GetNameSafe(ShatterSystem), *GetNameSafe(ShatterSound));

	if (ShatterSystem)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, ShatterSystem, SpawnLocation, FRotator::ZeroRotator);
	}
	if (ShatterSound)
	{
		UGameplayStatics::PlaySoundAtLocation(World, ShatterSound, SpawnLocation);
	}
	return true;
}
