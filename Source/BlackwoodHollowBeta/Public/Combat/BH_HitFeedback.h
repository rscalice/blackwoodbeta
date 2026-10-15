
// Blackwood Hollow - HitFeedback: one LOCAL event per resolved combat outcome, for Blueprint audio / VFX / camera / rumble (Phase 13F-1)
// Target: Unreal Engine 5.8 (C++), GAS
//
// WHY IT IS NOT A NEW RPC: every hit, block, perfect block and posture break already reaches every machine as a GameplayCue
// (UBH_GCN_CombatHit / UBH_GCN_ParrySuccess / UBH_GCN_PostureBroken, fired by the server through the ASC). Those cues call
// UBH_HitFeedbackSubsystem::Report on each machine, so the event fires on the host AND on every client with no extra network traffic,
// and it can never disagree with the hit-stop / shake the same cue already plays. Cosmetic only; nothing here changes gameplay.
//
// USE FROM BLUEPRINT (13F-2..4): "Get HitFeedback Subsystem" -> "Assign On Hit Feedback" (e.g. in the player controller or a feel Blueprint).
// The payload carries everything a sound / VFX / shake graph needs:
//   Tier ...... EBH_ImpactTier, the same tier DA_CombatFeel uses for hit-freeze (Light / Medium / Heavy / Massive). A fatal hit is at
//               least Heavy and also sets bFatal. A posture break is Massive. A perfect block is Heavy.
//   Surface ... what the VICTIM is made of (Flesh / Armour / Shell), from UBH_CombatIdentityComponent::SurfaceType or
//               ABH_EnemyBase::SurfaceType (the crabs are Shell). Players are Flesh.
//   bBlighted . the victim is a Blighted enemy (purple ichor instead of red blood).
//   Outcome ... Hit / Block / PerfectBlock / PostureBreak.
//   Attacker / Victim ... for PerfectBlock the Victim is the defender who blocked; for PostureBreak the Victim is the actor whose posture broke.
//   bAttackerLocallyControlled / bVictimLocallyControlled ... gate camera shake and controller rumble on these: ONLY the machine whose own
//               pawn is the attacker or the victim should shake or rumble. Sounds and sparks can play for everyone.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Subsystems/WorldSubsystem.h"
#include "Combat/BH_CombatFeel.h"
#include "BH_HitFeedback.generated.h"

class AActor;

/** What a victim is made of; picks the impact sound / spark set. Set per enemy (UBH_CombatIdentityComponent::SurfaceType, ABH_EnemyBase::SurfaceType). */
UENUM(BlueprintType)
enum class EBH_HitSurface : uint8
{
	Flesh,
	Armour,
	Shell
};

/** The outcome a hit resolved to. */
UENUM(BlueprintType)
enum class EBH_HitOutcome : uint8
{
	Hit,
	Block,
	PerfectBlock,
	PostureBreak
};

USTRUCT(BlueprintType)
struct BLACKWOODHOLLOWBETA_API FBH_HitFeedback
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "HitFeedback")
	TObjectPtr<AActor> Attacker = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "HitFeedback")
	TObjectPtr<AActor> Victim = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "HitFeedback")
	EBH_ImpactTier Tier = EBH_ImpactTier::Light;

	UPROPERTY(BlueprintReadOnly, Category = "HitFeedback")
	EBH_HitSurface Surface = EBH_HitSurface::Flesh;

	UPROPERTY(BlueprintReadOnly, Category = "HitFeedback")
	bool bIsBlighted = false;

	UPROPERTY(BlueprintReadOnly, Category = "HitFeedback")
	EBH_HitOutcome Outcome = EBH_HitOutcome::Hit;

	/** The hit killed the victim (the tier is then at least Heavy). */
	UPROPERTY(BlueprintReadOnly, Category = "HitFeedback")
	bool bFatal = false;

	/** Impact point (blade point if known, else chest height on the victim; the midpoint for a perfect block). */
	UPROPERTY(BlueprintReadOnly, Category = "HitFeedback")
	FVector Location = FVector::ZeroVector;

	/** Attacker toward victim (zero if unknown). */
	UPROPERTY(BlueprintReadOnly, Category = "HitFeedback")
	FVector Direction = FVector::ZeroVector;

	/** The attacker is a pawn controlled by a local player on THIS machine. Gate camera shake / rumble on this and bVictimLocallyControlled. */
	UPROPERTY(BlueprintReadOnly, Category = "HitFeedback")
	bool bAttackerLocallyControlled = false;

	/** The victim is a pawn controlled by a local player on THIS machine. */
	UPROPERTY(BlueprintReadOnly, Category = "HitFeedback")
	bool bVictimLocallyControlled = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBH_OnHitFeedback, const FBH_HitFeedback&, Feedback);

/** Per-world hub for the local HitFeedback event. Native code reports into it from the combat cues; Blueprints assign OnHitFeedback. */
UCLASS()
class BLACKWOODHOLLOWBETA_API UBH_HitFeedbackSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Fires on every machine for every resolved hit, block, perfect block and posture break (never on a dedicated server). */
	UPROPERTY(BlueprintAssignable, Category = "HitFeedback")
	FBH_OnHitFeedback OnHitFeedback;

	/** The subsystem of the world WorldContext lives in, or null. */
	UFUNCTION(BlueprintPure, Category = "HitFeedback", meta = (WorldContext = "WorldContext", DisplayName = "Get HitFeedback Subsystem"))
	static UBH_HitFeedbackSubsystem* GetHitFeedbackSubsystem(const UObject* WorldContext);

	/**
	 * Builds the payload (surface and blight from the victim, local-control flags from the pawns) and broadcasts OnHitFeedback.
	 * Called by the combat cues; safe to call from anywhere cosmetic. No-op on a dedicated server.
	 */
	static void Report(const UObject* WorldContext, AActor* Attacker, AActor* Victim, EBH_ImpactTier Tier, EBH_HitOutcome Outcome,
		bool bFatal, const FVector& Location, const FVector& Direction);
};

UCLASS()
class BLACKWOODHOLLOWBETA_API UBH_HitFeedbackLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** What Actor is made of: its identity component's SurfaceType, else ABH_EnemyBase::SurfaceType, else Flesh (players). */
	UFUNCTION(BlueprintPure, Category = "HitFeedback")
	static EBH_HitSurface GetHitSurface(const AActor* Actor);

	/** True if Actor is a Blighted enemy (identity component bBlighted, else ABH_EnemyBase::bBlighted). */
	UFUNCTION(BlueprintPure, Category = "HitFeedback")
	static bool IsActorBlighted(const AActor* Actor);
};
