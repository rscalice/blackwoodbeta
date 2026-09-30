// Blackwood Hollow - Combat GameplayCue notifies (cosmetic, run on every machine)
// Target: Unreal Engine 5.8 (C++), GAS
//
// Cue manager discovery: native UGameplayCueNotify_Static classes are NOT found by tag on their own.
// The GameplayCueManager only registers Blueprint notify assets under its scanned paths (default /Game),
// so each cue needs a Blueprint subclass with GameplayCueTag set:
//   /Game/BlackwoodHollow/Combat/Cues/GC_Combat_Hit           (parent UBH_GCN_CombatHit,      tag GameplayCue.Combat.Hit)
//   /Game/BlackwoodHollow/Combat/Cues/GC_Combat_ParrySuccess  (parent UBH_GCN_ParrySuccess,   tag GameplayCue.Combat.ParrySuccess)
//   /Game/BlackwoodHollow/Combat/Cues/GC_Combat_PostureBroken (parent UBH_GCN_PostureBroken,  tag GameplayCue.Combat.PostureBroken)
// Tunables (shake classes, durations, Niagara/sound) live on those Blueprint CDOs.
//
// Parameter convention (see UAH_GA_MeleeAttack_Base::OnHitDealt / UAH_GA_Parry / UAH_AttributeSet):
//   Hit:           MyTarget = attacker, SourceObject = victim, Normal = attacker->victim, RawMagnitude = damage.
//   ParrySuccess:  MyTarget = parrier,  SourceObject = Instigator = EffectCauser = attacker.
//   PostureBroken: MyTarget = the actor whose posture broke.

#pragma once

#include "CoreMinimal.h"
#include "GameplayCueNotify_Static.h"
#include "Camera/CameraShakeBase.h"
#include "BH_GCN_Combat.generated.h"

class UNiagaraSystem;
class USoundBase;

/** Hit-stop on attacker + victim and a directional camera shake for the local player if involved. */
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
};

/** Spawns ShatterSystem and plays ShatterSound at the target (both optional / null by default). */
UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API UBH_GCN_PostureBroken : public UGameplayCueNotify_Static
{
	GENERATED_BODY()

public:
	UBH_GCN_PostureBroken();

	virtual bool OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Posture")
	TObjectPtr<UNiagaraSystem> ShatterSystem;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Posture")
	TObjectPtr<USoundBase> ShatterSound;

	/** Offset from the target's origin (world Z up) where the effect spawns. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Posture")
	FVector SpawnOffset = FVector(0.0, 0.0, 90.0);
};
