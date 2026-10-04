// Blackwood Hollow - combat "juice": impact tiers (camera shake / hit-stop / flash), voice sets, trail tint tables.
// Target: Unreal Engine 5.8 (C++)
//
// TIER SELECTION (the single place; everything else only calls into this):
//   Dealt hit by the local player ........ TierForHit: damage < LightDamageMax -> Light, < MediumDamageMax -> Medium, else Heavy.
//                                          Finisher (step posture multiplier > FinisherPostureMultiplier) -> Heavy.
//   Blocked hit .......................... Light, or Medium if the blocker's stamina is 0.
//   Received by the local player ......... one tier higher than the dealt tier (PlayImpactFeel bEscalateForVictim, not for blocked hits).
//   Shield bash .......................... Medium (UBH_GCN_CombatHit::bUseFixedTier on GC_Combat_ShieldBashHit).
//   Parry success ........................ Heavy, shake only for the parrier (bInstigatorOnlyShake).
//   Posture broken ....................... Massive for the local player when they break someone or are broken.
//   Kill ................................. a fatal hit is at least Heavy.
// Hit-stop uses the un-escalated tier; the shake uses the escalated one. Both come from DA_CombatFeel.
//
// TWO-FIGHTER ANIMATION FREEZE (the "hit-stop"): a cosmetic freeze of BOTH fighters' skeletal mesh animation, set on every
// machine from the hit cue. It sets USkeletalMeshComponent::GlobalAnimRateScale (near zero) for a per-tier duration from
// DA_CombatFeel (Freeze* properties). Victim freezes FreezeVictimFactor x longer, blocked hits FreezeBlockedFactor x.
// It never touches actor or global time dilation, so movement, physics and server timers keep running.
// Overlap safe (BH_CueUtils::ApplyHitStop): the original rate is saved once, the freeze lasts until the LATEST end time.
//
// TIERED PUSHBACK (server, gameplay): UBH_CombatFeelLibrary::ApplyHitPushback, called from UAH_GA_MeleeAttack_Base::OnHitDealt
// after damage. Additive constant-force root motion on the victim's CharacterMovement, horizontal only, distance / duration
// per tier x stance multiplier (x BlockedPushbackFactor when blocked). Remote client victims get an identical source applied
// locally through ABH_CharacterBase::Client_ApplyHitPushback so their prediction agrees with the server.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Engine/DataAsset.h"
#include "Camera/CameraShakeBase.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BH_CombatFeel.generated.h"

class USoundBase;
class USoundAttenuation;
class UMaterialInterface;
class UBH_VoiceSetDataAsset;

DECLARE_LOG_CATEGORY_EXTERN(LogBHFeel, Log, All);

UENUM(BlueprintType)
enum class EBH_ImpactTier : uint8
{
	Light,
	Medium,
	Heavy,
	Massive
};

UENUM(BlueprintType)
enum class EBH_VoiceCategory : uint8
{
	AttackLight,
	AttackHeavy,
	Hurt,
	HurtHeavy,
	PostureBreak,
	Dodge,
	Death
};

USTRUCT(BlueprintType)
struct BLACKWOODHOLLOWBETA_API FBH_ImpactTierSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact")
	TSubclassOf<UCameraShakeBase> ShakeClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact", meta = (ClampMin = "0.0"))
	float ShakeScale = 1.f;

	/** LEGACY freeze length. Only used when UBH_CombatFeelSettings::FreezeDurationByTier has no entry for this tier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact", meta = (ClampMin = "0.0"))
	float HitStopDuration = 0.05f;

	/** Point light intensity (candela-ish, unitless) of the impact flash. 0 = no flash (Light / Medium). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact", meta = (ClampMin = "0.0"))
	float FlashIntensity = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact")
	FLinearColor FlashColor = FLinearColor(1.f, 0.65f, 0.3f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact", meta = (ClampMin = "0.0"))
	float FlashRadius = 350.f;
};

/** Per-character sound arrays for the vocal categories (attack efforts, hurt, death...). Empty arrays are silent (TODO: author sounds). */
UCLASS(BlueprintType)
class BLACKWOODHOLLOWBETA_API UBH_VoiceSetDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice")
	TArray<TObjectPtr<USoundBase>> AttackLight;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice")
	TArray<TObjectPtr<USoundBase>> AttackHeavy;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice")
	TArray<TObjectPtr<USoundBase>> Hurt;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice")
	TArray<TObjectPtr<USoundBase>> HurtHeavy;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice")
	TArray<TObjectPtr<USoundBase>> PostureBreak;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice")
	TArray<TObjectPtr<USoundBase>> Dodge;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice")
	TArray<TObjectPtr<USoundBase>> Death;

	/** Multiplies the pitch of every sound in this set (Echo 0.9, Vanguard 0.85). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice", meta = (ClampMin = "0.25", ClampMax = "2.0"))
	float PitchMultiplier = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice", meta = (ClampMin = "0.0"))
	float VolumeMultiplier = 1.f;

	const TArray<TObjectPtr<USoundBase>>& GetSounds(EBH_VoiceCategory Category) const;
};

/** Global combat-feel tuning, /Game/BlackwoodHollow/Combat/Data/DA_CombatFeel. Defaults are filled in the constructor so the code works even without the asset. */
UCLASS(BlueprintType)
class BLACKWOODHOLLOWBETA_API UBH_CombatFeelSettings : public UDataAsset
{
	GENERATED_BODY()

public:
	UBH_CombatFeelSettings();

	/** The DA_CombatFeel asset (loaded once); a transient defaults object if it does not exist. */
	static const UBH_CombatFeelSettings* Get();

	/** Indexed by EBH_ImpactTier (Light, Medium, Heavy, Massive). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impact")
	TArray<FBH_ImpactTierSettings> Tiers;

	/** Multiplies every shake (together with CVar bh.Combat.ShakeScale). Accessibility hook. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impact", meta = (ClampMin = "0.0"))
	float ShakeScaleMultiplier = 1.f;

	/** A local player who is neither attacker nor victim still feels hits within this distance (cm), with falloff. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impact", meta = (ClampMin = "0.0"))
	float SpectatorShakeRadius = 1500.f;

	/** Maximum shake scale for such spectators. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impact", meta = (ClampMin = "0.0"))
	float SpectatorShakeScale = 0.3f;

	// -- Two-fighter animation freeze (hit-stop) ------------------------------------------

	/** Master switch for the animation freeze. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impact|Freeze")
	bool bEnableAnimFreeze = true;

	/** Seconds of freeze per tier (Light, Medium, Heavy, Massive), for the attacker; see FreezeVictimFactor. A missing entry falls back to the tier's HitStopDuration. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impact|Freeze")
	TArray<float> FreezeDurationByTier;

	/** Multiplier on the freeze length for the attacker. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impact|Freeze", meta = (ClampMin = "0.0"))
	float FreezeAttackerFactor = 1.f;

	/** Multiplier on the freeze length for the victim (the victim holds the pose a little longer, which sells the hit). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impact|Freeze", meta = (ClampMin = "0.0"))
	float FreezeVictimFactor = 1.15f;

	/** Multiplier on the freeze length when the hit was blocked. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impact|Freeze", meta = (ClampMin = "0.0"))
	float FreezeBlockedFactor = 0.6f;

	/** GlobalAnimRateScale during the freeze (0 = fully stopped; a hair above 0 keeps a faint crawl). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impact|Freeze", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float FreezeAnimRateScale = 0.02f;

	/** Parry success: the freeze tier for the parried attacker (only the attacker freezes). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impact|Freeze")
	EBH_ImpactTier ParryFreezeTier = EBH_ImpactTier::Heavy;

	/** Freeze length for Tier (attacker, unmodified by the factors). */
	float GetFreezeDuration(EBH_ImpactTier Tier) const;

	// -- Tiered pushback ----------------------------------------------------------------

	/** Master switch for ApplyHitPushback. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impact|Pushback")
	bool bEnablePushback = true;

	/** Horizontal push distance in cm per tier (Light, Medium, Heavy, Massive). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impact|Pushback")
	TArray<float> PushbackDistanceByTier;

	/** Push duration in seconds per tier. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impact|Pushback")
	TArray<float> PushbackDurationByTier;

	/** A blocked hit pushes this fraction of the distance. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impact|Pushback", meta = (ClampMin = "0.0"))
	float BlockedPushbackFactor = 0.5f;

	/** Distance multiplier by the ATTACKER's weapon stance (Stance.Weapon.*). Unlisted stances use DefaultPushbackStanceMultiplier. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impact|Pushback", meta = (Categories = "Stance.Weapon"))
	TMap<FGameplayTag, float> PushbackStanceMultipliers;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impact|Pushback", meta = (ClampMin = "0.0"))
	float DefaultPushbackStanceMultiplier = 1.f;

	/** Send the owning client of a remote victim the same root motion source (unreliable) so its prediction matches the server. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impact|Pushback")
	bool bPredictPushbackOnOwningClient = true;

	/** Hit tier thresholds (see TierForHit). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impact|Tiers")
	float LightDamageMax = 15.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impact|Tiers")
	float MediumDamageMax = 25.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Impact|Tiers")
	float FinisherPostureMultiplier = 1.5f;

	// -- Voice ----------------------------------------------------------------

	/** Voice set for player-controlled pawns (enemies use UBH_CombatIdentityComponent::VoiceSet). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice")
	TObjectPtr<UBH_VoiceSetDataAsset> PlayerVoiceSet;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice")
	TObjectPtr<USoundAttenuation> VoiceAttenuation;

	/** Minimum seconds between two Hurt vocals (Hurt / HurtHeavy) on the same actor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice", meta = (ClampMin = "0.0"))
	float HurtVoiceInterval = 0.35f;

	/** Minimum seconds between two vocals of the same category group on one actor (attacks, dodge, posture). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Voice", meta = (ClampMin = "0.0"))
	float VoiceMinInterval = 0.2f;

	// -- Weapon trails --------------------------------------------------------

	/** Trail material per key: "SwordAndShield", "DualSword", "Greatsword" (overlay pose display names), "Echo", "Corrupted". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trail")
	TMap<FName, TObjectPtr<UMaterialInterface>> TrailMaterials;

	/** Seconds a ribbon sample lives (= tail fade after the swing). Keyed like TrailMaterials. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trail")
	TMap<FName, float> TrailLifetimes;

	/** Tag-keyed twins (Stance.Weapon.*) of TrailMaterials / TrailLifetimes; read first for the stance key, the legacy maps are the fallback ("Echo" / "Corrupted" stay legacy). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trail", meta = (Categories = "Stance.Weapon"))
	TMap<FGameplayTag, TObjectPtr<UMaterialInterface>> TrailMaterialsByTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trail", meta = (Categories = "Stance.Weapon"))
	TMap<FGameplayTag, float> TrailLifetimesByTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trail", meta = (ClampMin = "0.02"))
	float DefaultTrailLifetime = 0.12f;
};

UCLASS()
class BLACKWOODHOLLOWBETA_API UBH_CombatFeelLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** See the tier table at the top of this file. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Feel")
	static EBH_ImpactTier TierForHit(float Damage, float PostureMultiplier, bool bBlocked, bool bBlockerStaminaZero);

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Feel")
	static EBH_ImpactTier Escalate(EBH_ImpactTier Tier);

	/**
	 * Hit-stop on Instigator + Victim (every machine), camera shake for the LOCAL player only (when they are instigator or victim,
	 * or within SpectatorShakeRadius with distance falloff, capped at SpectatorShakeScale), and a brief flash light on Heavy+.
	 * @param bEscalateForVictim  the shake is one tier higher when the local player is the victim.
	 * @param bInstigatorOnlyShake the shake plays only for the local player when they are the instigator (parry).
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Feel", meta = (WorldContext = "WorldContext"))
	static void PlayImpactFeel(const UObject* WorldContext, EBH_ImpactTier Tier, AActor* Instigator, AActor* Victim, FVector Location,
		bool bEscalateForVictim = false, bool bInstigatorOnlyShake = false, bool bBlocked = false, bool bSkipFreeze = false);

	/**
	 * Cosmetic animation freeze of one actor for the Tier's duration x Factor (x FreezeBlockedFactor when bBlocked).
	 * Skipped on dedicated servers. See the freeze notes at the top of this file.
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Feel")
	static void ApplyTierFreeze(AActor* Actor, EBH_ImpactTier Tier, float Factor = 1.f, bool bBlocked = false);

	/**
	 * Server only. Pushes Victim away from Attacker (horizontal) by the Tier's distance x attacker-stance multiplier
	 * (x BlockedPushbackFactor when bBlocked) with a constant-force root motion source. No-op for dead / posture-broken victims,
	 * non-characters and non-authority callers. Remote client victims also get Client_ApplyHitPushback.
	 * @return true if a push was applied.
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Feel")
	static bool ApplyHitPushback(AActor* Attacker, AActor* Victim, EBH_ImpactTier Tier, bool bBlocked);

	/**
	 * Applies the pushback root motion source on Character's movement component (any machine). Direction is flattened and
	 * normalised; Id makes the InstanceName so a server source and its client twin match each other. Returns false if invalid.
	 */
	static bool ApplyPushbackSource(class ACharacter* Character, const FVector& Direction, float Distance, float Duration, uint16 Id);

	/** The voice set for Actor (identity component's VoiceSet, or the player's default). May be null. */
	static const UBH_VoiceSetDataAsset* ResolveVoiceSet(const AActor* Actor);

	/**
	 * Plays one random vocal of Category from Actor's voice set at the actor (head height), subject to Chance and the per-actor rate limit.
	 * Always logs the attempt to LogBHFeel (Verbose). @return the sound played (null if gated / the set has none).
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Feel")
	static USoundBase* PlayVoice(AActor* Actor, EBH_VoiceCategory Category, float Chance = 1.f);
};
