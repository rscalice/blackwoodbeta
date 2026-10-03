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

	/** Seconds both actors' skeletal meshes freeze. */
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
		bool bEscalateForVictim = false, bool bInstigatorOnlyShake = false);

	/** The voice set for Actor (identity component's VoiceSet, or the player's default). May be null. */
	static const UBH_VoiceSetDataAsset* ResolveVoiceSet(const AActor* Actor);

	/**
	 * Plays one random vocal of Category from Actor's voice set at the actor (head height), subject to Chance and the per-actor rate limit.
	 * Always logs the attempt to LogBHFeel (Verbose). @return the sound played (null if gated / the set has none).
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Feel")
	static USoundBase* PlayVoice(AActor* Actor, EBH_VoiceCategory Category, float Chance = 1.f);
};
