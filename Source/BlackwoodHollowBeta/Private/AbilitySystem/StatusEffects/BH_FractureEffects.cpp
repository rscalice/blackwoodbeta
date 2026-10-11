// Blackwood Hollow - Heart-Fragment Fracture (implementation)

#include "AbilitySystem/StatusEffects/BH_FractureEffects.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "Player/BH_PartyLibrary.h"
#include "UObject/UObjectGlobals.h"

UE_DEFINE_GAMEPLAY_TAG_COMMENT(TAG_State_Status_Fracture, "State.Status.Fracture",
	"Heart-Fragment Fracture: -10% Max Posture, -25% shielding efficiency (applied to the party on a hub respawn)");

UAH_GE_Fracture::UAH_GE_Fracture()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;

	DisplayName = NSLOCTEXT("BlackwoodHollow", "Status_Fracture", "Fracture");
	Description = NSLOCTEXT("BlackwoodHollow", "Status_Fracture_Desc",
		"Your Heart Fragment is cracked: shielding is 25% weaker and your Max Posture is 10% lower. Repair it at Port Vanguard.");
	bIsDebuff = true;

	// MultiplyAdditive: final = base * (1 + sum(mult - 1)), same convention as the armor-weight / Rot stamina penalty effects.
	FGameplayModifierInfo Modifier;
	Modifier.Attribute = UAH_AttributeSet::GetMaxPostureAttribute();
	Modifier.ModifierOp = EGameplayModOp::MultiplyAdditive;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(MaxPostureMultiplier));
	Modifiers.Add(Modifier);

	// One Fracture per target: re-application is ignored by the library (tag check) and capped here as a second line of defence.
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	StackingType = EGameplayEffectStackingType::AggregateByTarget;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	StackLimitCount = 1;
	StackDurationRefreshPolicy = EGameplayEffectStackingDurationPolicy::NeverRefresh;
	StackExpirationPolicy = EGameplayEffectStackingExpirationPolicy::ClearEntireStack;

	UTargetTagsGameplayEffectComponent* GrantedTags = CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("FractureGrantedTags"));
	ConfigureStatusTag(GrantedTags, TAG_State_Status_Fracture);
	GEComponents.Add(GrantedTags);
}

// ============================================================================
// Library
// ============================================================================

UAbilitySystemComponent* UBH_FractureLibrary::ResolveASC(const AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}
	if (UAbilitySystemComponent* Direct = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor))
	{
		return Direct;
	}
	if (const APawn* AsPawn = Cast<APawn>(Actor))
	{
		return UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(AsPawn->GetPlayerState());
	}
	return nullptr;
}

bool UBH_FractureLibrary::IsFractured(const UAbilitySystemComponent* ASC)
{
	return ASC && ASC->HasMatchingGameplayTag(TAG_State_Status_Fracture);
}

bool UBH_FractureLibrary::IsActorFractured(const AActor* Actor)
{
	return IsFractured(ResolveASC(Actor));
}

float UBH_FractureLibrary::GetShieldingEfficiencyMultiplier(const UAbilitySystemComponent* ASC)
{
	return IsFractured(ASC) ? UAH_GE_Fracture::ShieldingEfficiencyMultiplier : 1.f;
}

TSubclassOf<UGameplayEffect> UBH_FractureLibrary::ResolveFractureEffectClass()
{
	// Optional Blueprint child (carries the HUD icon). Quiet when the asset does not exist.
	static const TCHAR* BlueprintPath = TEXT("/Game/BlackwoodHollow/Blueprints/Status/GE_Fracture.GE_Fracture_C");
	if (UClass* BlueprintClass = StaticLoadClass(UGameplayEffect::StaticClass(), nullptr, BlueprintPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		return BlueprintClass;
	}
	return UAH_GE_Fracture::StaticClass();
}

bool UBH_FractureLibrary::ApplyFracture(UAbilitySystemComponent* ASC)
{
	if (!ASC || !ASC->IsOwnerActorAuthoritative() || IsFractured(ASC))
	{
		return false;
	}

	const TSubclassOf<UGameplayEffect> EffectClass = ResolveFractureEffectClass();
	if (!EffectClass)
	{
		return false;
	}
	const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(EffectClass, 1.f, ASC->MakeEffectContext());
	if (!Spec.IsValid() || !Spec.Data.IsValid())
	{
		return false;
	}
	return ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get()).IsValid();
}

bool UBH_FractureLibrary::RemoveFracture(UAbilitySystemComponent* ASC)
{
	if (!ASC || !ASC->IsOwnerActorAuthoritative())
	{
		return false;
	}
	const bool bWasFractured = IsFractured(ASC);
	FGameplayTagContainer FractureTags;
	FractureTags.AddTag(TAG_State_Status_Fracture);
	ASC->RemoveActiveEffectsWithGrantedTags(FractureTags);
	return bWasFractured;
}

int32 UBH_FractureLibrary::ApplyFractureToParty(const UObject* WorldContext)
{
	int32 NewlyFractured = 0;
	UBH_PartyLibrary::ForEachPartyMember(WorldContext, [&NewlyFractured](APlayerState& PlayerState, APawn* MemberPawn)
	{
		UAbilitySystemComponent* MemberASC = ResolveASC(MemberPawn);
		if (!MemberASC)
		{
			MemberASC = ResolveASC(&PlayerState);
		}
		if (ApplyFracture(MemberASC))
		{
			++NewlyFractured;
		}
	});
	return NewlyFractured;
}
