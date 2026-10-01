// Blackwood Hollow - Combat GameplayCue notifies (cosmetic, run on every machine)
// Target: Unreal Engine 5.8 (C++), GAS
//
// Cue manager discovery: native UGameplayCueNotify_Static classes are NOT found by tag on their own.
// The GameplayCueManager only registers Blueprint notify assets under its scanned paths (default /Game),
// so each cue needs a Blueprint subclass with GameplayCueTag set:
//   /Game/BlackwoodHollow/Combat/Cues/GC_Combat_Hit           (parent UBH_GCN_CombatHit,      tag GameplayCue.Combat.Hit)
//   /Game/BlackwoodHollow/Combat/Cues/GC_Combat_ParrySuccess  (parent UBH_GCN_ParrySuccess,   tag GameplayCue.Combat.ParrySuccess)
//   /Game/BlackwoodHollow/Combat/Cues/GC_Combat_PostureBroken (parent UBH_GCN_PostureBroken,  tag GameplayCue.Combat.PostureBroken)
//   /Game/BlackwoodHollow/Combat/Cues/GC_Combat_ShieldBashHit (parent UBH_GCN_CombatHit,      tag GameplayCue.Combat.Hit.ShieldBash)
// Tunables (shake classes, durations, Niagara/sound) live on those Blueprint CDOs.
//
// Parameter convention (see UAH_GA_MeleeAttack_Base::OnHitDealt / UAH_GA_Parry / UAH_AttributeSet):
//   Hit:           MyTarget = attacker, SourceObject = victim, Normal = attacker->victim, RawMagnitude = damage,
//                  Location = blade impact point (zero if unknown), AggregatedSourceTags has Combat.HitResult.Blocked
//                  when the victim blocked the hit. Abilities may fire a child tag (GameplayCue.Combat.Hit.ShieldBash)
//                  to get their own cue Blueprint; it is a UBH_GCN_CombatHit too, with its own FX.
//   ParrySuccess:  MyTarget = parrier,  SourceObject = Instigator = EffectCauser = attacker.
//   PostureBroken: MyTarget = the actor whose posture broke.

#pragma once

#include "CoreMinimal.h"
#include "GameplayCueNotify_Static.h"
#include "Camera/CameraShakeBase.h"
#include "Cues/BH_CueFX.h"
#include "BH_GCN_Combat.generated.h"

class USoundAttenuation;

/** Hit-stop on attacker + victim, a directional camera shake for the local player if involved, and impact FX. */
UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API UBH_GCN_CombatHit : public UGameplayCueNotify_Static
{
	GENERATED_BODY()

public:
	UBH_GCN_CombatHit();

	virtual bool OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const override;

	/** Seconds both actors' skeletal meshes freeze (GlobalAnimRateScale = 0). 0 disables. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hit", meta = (ClampMin = "0.0"))
	float HitStopDuration = 0.05f;

	/** Played on the local player's camera when the attacker or victim is their pawn. Default: UBH_CameraShake_Hit. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hit")
	TSubclassOf<UCameraShakeBase> HitShakeClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hit", meta = (ClampMin = "0.0"))
	float HitShakeScale = 1.f;

	/** Impact FX when the victim took the hit (blood, flesh sounds). Spawned at the impact point, oriented along the hit normal. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hit|FX")
	FBH_CueFX FleshFX;

	/** Impact FX when the victim blocked the hit (Combat.HitResult.Blocked in the cue's source tags). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hit|FX")
	FBH_CueFX BlockedFX;

	/** Spatialization for this cue's sounds. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hit|FX")
	TObjectPtr<USoundAttenuation> Attenuation;

	/** Fallback spawn point when the cue carries no impact location: victim origin + this (about chest height). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hit|FX")
	FVector FallbackVictimOffset = FVector(0.0, 0.0, 110.0);

	/**
	 * Minimum seconds between impact SOUNDS on the same victim, so multi-hit attacks (spins) do not stack a full
	 * clip per hit. The Niagara VFX still plays on every hit. 0 disables the limit.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hit|FX", meta = (ClampMin = "0.0"))
	float MinSoundInterval = 0.1f;
};

/** Longer hit-stop on parrier + attacker and a heavier camera punch for the local parrier. */
UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API UBH_GCN_ParrySuccess : public UGameplayCueNotify_Static
{
	GENERATED_BODY()

public:
	UBH_GCN_ParrySuccess();

	virtual bool OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Parry", meta = (ClampMin = "0.0"))
	float HitStopDuration = 0.08f;

	/** Default: UBH_CameraShake_ParryPunch. Only played for the parrier (MyTarget) when they are the local pawn. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Parry")
	TSubclassOf<UCameraShakeBase> ParryShakeClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Parry", meta = (ClampMin = "0.0"))
	float ParryShakeScale = 1.f;

	/** Clash FX spawned between parrier and attacker (midpoint, raised by ParryFXHeight). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Parry|FX")
	FBH_CueFX ParryFX;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Parry|FX")
	TObjectPtr<USoundAttenuation> Attenuation;

	/** World Z added to the parrier/attacker midpoint (their origins are at the feet). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Parry|FX")
	float ParryFXHeight = 120.f;
};

/** Spawns PrimaryFX and SecondaryFX (system + random sound each) at the broken actor. */
UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API UBH_GCN_PostureBroken : public UGameplayCueNotify_Static
{
	GENERATED_BODY()

public:
	UBH_GCN_PostureBroken();

	virtual bool OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const override;

	/** Main effect (e.g. the expanding ring). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Posture")
	FBH_CueFX PrimaryFX;

	/** Accompanying effect (e.g. crystal shards). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Posture")
	FBH_CueFX SecondaryFX;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Posture")
	TObjectPtr<USoundAttenuation> Attenuation;

	/** Offset from the target's origin (world Z up) where the effect spawns. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Posture")
	FVector SpawnOffset = FVector(0.0, 0.0, 90.0);
};
