// Blackwood Hollow - Core combat AttributeSet
// Target: Unreal Engine 5.8 (C++), GAS (GameplayAbilities plugin required)

#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "AH_AttributeSet.generated.h"

// Boilerplate accessor macro (standard GAS pattern) -- generates getters/
// setters/initters for each attribute, e.g. GetHealth(), SetHealth(), etc.
#define ATTRIBUTE_ACCESSORS(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

/** Broadcast when Health hits zero via a GameplayEffect execution. */
DECLARE_MULTICAST_DELEGATE_OneParam(FAH_OnAttributeZero, AActor* /*EffectInstigator*/);

/**
 * UAH_AttributeSet
 *
 * Core combat attribute set for Blackwood Hollow characters (player Vanguards
 * and enemies alike). Covers vitality (Health/Mana/Posture), offense/defense
 * (AttackPower/Defense), and the world's signature resistance stat
 * (BlightResistance) used to mitigate Blight fog / Blight Volume damage.
 *
 * Posture follows a Sekiro/FromSoft-style stance-break model: it depletes on
 * blocked or grazing hits and, at zero, applies State.Combat.PostureBroken
 * and fires Event.Combat.PostureBreak so abilities/animations can react.
 */
UCLASS()
class BLACKWOODHOLLOWBETA_API UAH_AttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UAH_AttributeSet();

	// -- UAttributeSet interface ------------------------------------------
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	virtual void PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const override;
	virtual bool PreGameplayEffectExecute(FGameplayEffectModCallbackData& Data) override;
	virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;

	// -- Health --------------------------------------------------------------
	UPROPERTY(BlueprintReadOnly, Category = "AttributeSet|Health", ReplicatedUsing = OnRep_Health)
	FGameplayAttributeData Health;
	ATTRIBUTE_ACCESSORS(UAH_AttributeSet, Health)

	UPROPERTY(BlueprintReadOnly, Category = "AttributeSet|Health", ReplicatedUsing = OnRep_MaxHealth)
	FGameplayAttributeData MaxHealth;
	ATTRIBUTE_ACCESSORS(UAH_AttributeSet, MaxHealth)

	// -- Mana ------------------------------------------------------------
	UPROPERTY(BlueprintReadOnly, Category = "AttributeSet|Mana", ReplicatedUsing = OnRep_Mana)
	FGameplayAttributeData Mana;
	ATTRIBUTE_ACCESSORS(UAH_AttributeSet, Mana)

	UPROPERTY(BlueprintReadOnly, Category = "AttributeSet|Mana", ReplicatedUsing = OnRep_MaxMana)
	FGameplayAttributeData MaxMana;
	ATTRIBUTE_ACCESSORS(UAH_AttributeSet, MaxMana)

	// -- Posture (stance-break meter) -------------------------------------
	UPROPERTY(BlueprintReadOnly, Category = "AttributeSet|Posture", ReplicatedUsing = OnRep_Posture)
	FGameplayAttributeData Posture;
	ATTRIBUTE_ACCESSORS(UAH_AttributeSet, Posture)

	UPROPERTY(BlueprintReadOnly, Category = "AttributeSet|Posture", ReplicatedUsing = OnRep_MaxPosture)
	FGameplayAttributeData MaxPosture;
	ATTRIBUTE_ACCESSORS(UAH_AttributeSet, MaxPosture)

	// -- Offense / Defense -------------------------------------------------
	UPROPERTY(BlueprintReadOnly, Category = "AttributeSet|Combat", ReplicatedUsing = OnRep_AttackPower)
	FGameplayAttributeData AttackPower;
	ATTRIBUTE_ACCESSORS(UAH_AttributeSet, AttackPower)

	UPROPERTY(BlueprintReadOnly, Category = "AttributeSet|Combat", ReplicatedUsing = OnRep_Defense)
	FGameplayAttributeData Defense;
	ATTRIBUTE_ACCESSORS(UAH_AttributeSet, Defense)

	// -- Blight resistance (mitigates BP_BlightVolume / Blight fog damage) -
	UPROPERTY(BlueprintReadOnly, Category = "AttributeSet|Blight", ReplicatedUsing = OnRep_BlightResistance)
	FGameplayAttributeData BlightResistance;
	ATTRIBUTE_ACCESSORS(UAH_AttributeSet, BlightResistance)

	// -- Meta attribute: incoming damage is routed through this and never
	// replicated directly; PostGameplayEffectExecute consumes it and applies
	// the net result to Health. Standard GAS "damage meta attribute" pattern.
	UPROPERTY(BlueprintReadOnly, Category = "AttributeSet|Meta", meta = (HideFromLevelInfos))
	FGameplayAttributeData IncomingDamage;
	ATTRIBUTE_ACCESSORS(UAH_AttributeSet, IncomingDamage)

	/** Fired (server-side) when Health is driven to <= 0 by a GameplayEffect. */
	FAH_OnAttributeZero OnHealthZero;

	/** Fired (server-side) when Posture is driven to <= 0 by a GameplayEffect. */
	FAH_OnAttributeZero OnPostureBroken;

protected:
	UFUNCTION()
	virtual void OnRep_Health(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	virtual void OnRep_MaxHealth(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	virtual void OnRep_Mana(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	virtual void OnRep_MaxMana(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	virtual void OnRep_Posture(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	virtual void OnRep_MaxPosture(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	virtual void OnRep_AttackPower(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	virtual void OnRep_Defense(const FGameplayAttributeData& OldValue);

	UFUNCTION()
	virtual void OnRep_BlightResistance(const FGameplayAttributeData& OldValue);

private:
	/** Shared clamp helper used by both PreAttributeChange and PostGameplayEffectExecute. */
	void ClampAttribute(const FGameplayAttribute& Attribute, float& NewValue) const;
};

#undef ATTRIBUTE_ACCESSORS
