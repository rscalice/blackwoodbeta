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
//   (Pushback is NOT a cue: it is applied on the server in OnHitDealt, see UBH_CombatFeelLibrary::ApplyHitPushback.)
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
#include "Combat/BH_CombatFeel.h"
#include "BH_GCN_Combat.generated.h"

class USoundAttenuation;

/**
 * Impact FX for a connecting hit + the tiered "feel" (hit-stop, camera shake, flash) via UBH_CombatFeelLibrary::PlayImpactFeel,
 * and the victim's hurt / death vocal. Cue params: RawMagnitude = damage, NormalizedMagnitude = step posture multiplier (finisher detection).
 */
UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API UBH_GCN_CombatHit : public UGameplayCueNotify_Static
{
	GENERATED_BODY()

public:
	UBH_GCN_CombatHit();

	virtual bool OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const override;

	/** Use FixedTier instead of the damage-based tier (shield bash = Medium). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hit")
	bool bUseFixedTier = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hit", meta = (EditCondition = "bUseFixedTier"))
	EBH_ImpactTier FixedTier = EBH_ImpactTier::Medium;

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

/** Heavy-tier feel (animation freeze on the parried attacker only, camera punch for the local parrier) and the clash FX. */
UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API UBH_GCN_ParrySuccess : public UGameplayCueNotify_Static
{
	GENERATED_BODY()

public:
	UBH_GCN_ParrySuccess();

	virtual bool OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const override;

	/** Clash FX spawned between parrier and attacker (midpoint, raised by ParryFXHeight). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Parry|FX")
	FBH_CueFX ParryFX;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Parry|FX")
	TObjectPtr<USoundAttenuation> Attenuation;

	/** World Z added to the parrier/attacker midpoint (their origins are at the feet). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Parry|FX")
	float ParryFXHeight = 120.f;
};

/** Spawns PrimaryFX and SecondaryFX (system + random sound each) at the broken actor, Massive-tier feel, and the broken actor's groan. */
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
