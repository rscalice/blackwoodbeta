// Blackwood Hollow - Melee hitbox AnimNotifyState
// Target: Unreal Engine 5.8 (C++), GAS (GameplayAbilities plugin required)

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Engine/EngineTypes.h"
#include "Engine/HitResult.h"
#include "Combat/BH_WeaponTypes.h"
#include "ANS_MeleeHitbox.generated.h"

class UMeshComponent;
class UPrimitiveComponent;

/**
 * UANS_MeleeHitbox
 *
 * Place over the active (damaging) frames of an attack. Every anim tick it
 * samples N points along the weapon from RootSocketName to TipSocketName and
 * sphere-sweeps each point from where it was last tick to where it is now, so
 * fast swings can't tunnel through thin targets.
 *
 * For every NEW actor hit (once per actor per notify instance by default):
 *   1) Event.Combat.Hit     -> sent to the HIT actor's ASC (pre-damage; UAH_GA_Parry hooks here)
 *   2) Event.Combat.HitDealt -> sent to the ATTACKER's ASC (UAH_GA_MeleeAttack_Base applies damage)
 * Both payloads carry Instigator = attacker, Target = victim, the FHitResult in
 * TargetData, and EventMagnitude = DamageMultiplier.
 *
 * Weapon points come from the mesh attached in WeaponSlot by
 * UBH_CombatFunctionLibrary::AttachWeaponMesh. If that mesh doesn't have the
 * root/tip sockets, the weapon's local bounds (longest axis) are used instead,
 * so un-socketed Fab meshes still work. As a last resort the notify looks for
 * the same socket names on the animating skeletal mesh itself.
 *
 * Note: UAnimNotifyState objects are shared by every mesh playing the same
 * animation, so per-swing state is kept in a map keyed by mesh component.
 */
UCLASS(meta = (DisplayName = "BH Melee Hitbox"))
class BLACKWOODHOLLOWBETA_API UANS_MeleeHitbox : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	UANS_MeleeHitbox();

	/** Which equipped weapon does the damage for this window. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hitbox")
	EBH_WeaponSlot WeaponSlot = EBH_WeaponSlot::MainHand;

	/** Socket at the base of the blade (on the weapon mesh). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hitbox")
	FName RootSocketName = TEXT("weapon_root");

	/** Socket at the tip of the blade (on the weapon mesh). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hitbox")
	FName TipSocketName = TEXT("weapon_tip");

	/** If the weapon mesh lacks the sockets, trace along its longest local bounds axis instead. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hitbox")
	bool bUseWeaponBoundsIfSocketsMissing = true;

	/** Radius of each swept sphere. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hitbox", meta = (ClampMin = "1.0"))
	float TraceRadius = 10.f;

	/** Points sampled along root->tip (min 2). More = better coverage on long blades. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hitbox", meta = (ClampMin = "2", ClampMax = "16"))
	int32 TraceSamples = 4;

	/** Object types the sweep can hit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hitbox")
	TArray<TEnumAsByte<ECollisionChannel>> HitObjectChannels;

	/** Scales this window's damage/posture (sent as EventMagnitude). E.g. 1.5 for a heavy finisher. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hitbox", meta = (ClampMin = "0.0"))
	float DamageMultiplier = 1.f;

	/** Each actor can only be hit once per notify window. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hitbox")
	bool bHitEachActorOnce = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hitbox|Debug")
	bool bDrawDebug = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hitbox|Debug", meta = (EditCondition = "bDrawDebug"))
	float DebugDrawDuration = 1.f;

	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration,
		const FAnimNotifyEventReference& EventReference) override;

	virtual void NotifyTick(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float FrameDeltaTime,
		const FAnimNotifyEventReference& EventReference) override;

	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

	virtual FString GetNotifyName_Implementation() const override;

private:
	struct FSwingState
	{
		TArray<FVector> PreviousPoints;
		TSet<TWeakObjectPtr<AActor>> HitActors;
	};

	/** Per-mesh swing state (this notify object is shared across all meshes playing the anim). */
	TMap<TWeakObjectPtr<USkeletalMeshComponent>, FSwingState> ActiveSwings;

	/** Fills OutPoints (world space) along the weapon. Returns false if no usable weapon points. */
	bool GetWeaponPoints(USkeletalMeshComponent* MeshComp, TArray<FVector>& OutPoints) const;

	void ProcessHit(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, FSwingState& Swing, const FHitResult& Hit) const;

	void SweepSegment(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, FSwingState& Swing,
		const FVector& Start, const FVector& End) const;
};
