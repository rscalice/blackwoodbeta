// Blackwood Hollow - status effect framework (Phase 10B)
// Target: Unreal Engine 5.8 (C++), GAS (GameplayAbilities plugin required)
//
// A status effect is any GameplayEffect that derives from UBH_GE_StatusEffect and grants one State.Status.* tag while it is
// active (Blight Rot is the first user: UAH_GE_BlightRot; Warden's Incense and Aegis plug in the same way later).
// The base class carries the display data the HUD needs, so the effect CLASS is its own definition (no registry, no data asset):
//
//   StatusTag     the State.Status.* tag the effect grants (set it in the constructor via ConfigureStatusTag, or on a Blueprint
//                 child -- then also add the same tag to the GE's "Grant tags to target actor" list)
//   DisplayName   shown in the HUD
//   Description   tooltip text
//   Icon          soft Texture2D, loaded by the widget when the status appears
//   bIsDebuff     lets the HUD colour / sort buffs and debuffs differently
//
// UBH_StatusEffectLibrary lists the active status effects of an ASC with their remaining time (read straight from the replicated
// ActiveGameplayEffects, so it works on the owning client as well as the server):
//
//   TArray<FBH_ActiveStatusEffect> Active;  UBH_StatusEffectLibrary::GetActiveStatusEffects(ASC, Active);
//
// Remaining < 0 means the effect has no expiry (infinite).

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "GameplayEffectTypes.h"
#include "GameplayTagContainer.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BH_StatusEffect.generated.h"

class UAbilitySystemComponent;
class UTargetTagsGameplayEffectComponent;
class UTexture2D;

/** Base class of every status effect. Derive in C++ (see UAH_GE_BlightRot) or as a Blueprint GE. */
UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API UBH_GE_StatusEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UBH_GE_StatusEffect();

	/** The State.Status.* tag this effect grants while active (the HUD / library identify the status by it). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BlackwoodHollow|Status", meta = (Categories = "State.Status"))
	FGameplayTag StatusTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BlackwoodHollow|Status")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BlackwoodHollow|Status")
	FText Description;

	/** HUD icon (soft: only loaded when a widget actually shows the status). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BlackwoodHollow|Status")
	TSoftObjectPtr<UTexture2D> Icon;

	/** True for harmful statuses (Blight Rot), false for buffs (Warden's Incense, Aegis). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "BlackwoodHollow|Status")
	bool bIsDebuff = true;

protected:
	/**
	 * Constructor helper for C++ subclasses: stores Tag as StatusTag and configures TagsComponent (a
	 * CreateDefaultSubobject'd UTargetTagsGameplayEffectComponent that the caller adds to GEComponents) to grant it.
	 */
	void ConfigureStatusTag(UTargetTagsGameplayEffectComponent* TagsComponent, const FGameplayTag& Tag);
};

/** One active status effect on an ASC, as listed by UBH_StatusEffectLibrary. */
USTRUCT(BlueprintType)
struct BLACKWOODHOLLOWBETA_API FBH_ActiveStatusEffect
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Status")
	FGameplayTag StatusTag;

	UPROPERTY(BlueprintReadOnly, Category = "Status")
	FText DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "Status")
	TSoftObjectPtr<UTexture2D> Icon;

	/** Seconds left; < 0 = no expiry. */
	UPROPERTY(BlueprintReadOnly, Category = "Status")
	float Remaining = -1.f;

	/** Total duration of the current application; < 0 = no expiry. */
	UPROPERTY(BlueprintReadOnly, Category = "Status")
	float Duration = -1.f;

	UPROPERTY(BlueprintReadOnly, Category = "Status")
	int32 StackCount = 1;

	UPROPERTY(BlueprintReadOnly, Category = "Status")
	bool bIsDebuff = true;

	UPROPERTY(BlueprintReadOnly, Category = "Status")
	FActiveGameplayEffectHandle Handle;
};

UCLASS()
class BLACKWOODHOLLOWBETA_API UBH_StatusEffectLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Every active UBH_GE_StatusEffect on ASC with its remaining time. Safe on the server and the owning client. OutEffects is cleared first. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Status")
	static void GetActiveStatusEffects(const UAbilitySystemComponent* ASC, TArray<FBH_ActiveStatusEffect>& OutEffects);

	/** The active status identified by StatusTag (exact match). @return false when it is not active (Out is left default). */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Status")
	static bool GetStatusEffect(const UAbilitySystemComponent* ASC, FGameplayTag StatusTag, FBH_ActiveStatusEffect& OutEffect);

	/** Seconds left of the status identified by StatusTag: 0 when it is not active, < 0 when it has no expiry. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Status")
	static float GetStatusRemainingTime(const UAbilitySystemComponent* ASC, FGameplayTag StatusTag);
};
