// Blackwood Hollow - enemy readability helpers (implementation, Phase 13D)

#include "UI/BH_EnemyReadability.h"
#include "UI/BH_EnemyReadabilitySettings.h"
#include "Characters/BH_EnemyBase.h"
#include "Combat/BH_CombatIdentityComponent.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Engine/Texture2D.h"
#include "Sound/SoundBase.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "BHEnemyReadability"

UBH_EnemyReadabilitySettings::UBH_EnemyReadabilitySettings()
{
	CategoryName = TEXT("Game");
	PaletteAsset = TSoftObjectPtr<UObject>(FSoftObjectPath(TEXT("/Game/BlackwoodHollow/UI/Style/DA_UI_Palette.DA_UI_Palette")));

	// One telegraph colour per archetype (palette variable names). Skirmisher reads as the quick flank (orange), Soldier as the standard
	// threat (Crimson, the "harm" colour), Brute as the Blight-heavy slam (BlightGlow). Change here, nowhere else.
	TelegraphColorTokens.Add(EBH_EnemyArchetype::Skirmisher, TEXT("LevelOrange"));
	TelegraphColorTokens.Add(EBH_EnemyArchetype::Soldier, TEXT("Crimson"));
	TelegraphColorTokens.Add(EBH_EnemyArchetype::Brute, TEXT("BlightGlow"));
}

FText UBH_EnemyReadabilityLibrary::RankLabel(EBH_EnemyRank Rank)
{
	switch (Rank)
	{
	case EBH_EnemyRank::Veteran: return LOCTEXT("RankVeteran", "Veteran");
	case EBH_EnemyRank::Elite: return LOCTEXT("RankElite", "Elite");
	case EBH_EnemyRank::Boss: return LOCTEXT("RankBoss", "Boss");
	default: return LOCTEXT("RankCommon", "Common");
	}
}

FText UBH_EnemyReadabilityLibrary::ArchetypeLabel(EBH_EnemyArchetype Archetype)
{
	switch (Archetype)
	{
	case EBH_EnemyArchetype::Skirmisher: return LOCTEXT("ArchSkirmisher", "Skirmisher");
	case EBH_EnemyArchetype::Soldier: return LOCTEXT("ArchSoldier", "Soldier");
	case EBH_EnemyArchetype::Brute: return LOCTEXT("ArchBrute", "Brute");
	case EBH_EnemyArchetype::Defender: return LOCTEXT("ArchDefender", "Defender");
	case EBH_EnemyArchetype::Caster: return LOCTEXT("ArchCaster", "Caster");
	default: return LOCTEXT("ArchSwarm", "Swarm");
	}
}

EBH_LevelTier UBH_EnemyReadabilityLibrary::GetLevelTier(int32 EnemyLevel, int32 ViewerLevel)
{
	const UBH_EnemyReadabilitySettings* S = UBH_EnemyReadabilitySettings::Get();
	const int32 Diff = EnemyLevel - ViewerLevel;
	if (Diff <= S->GreyMaxDiff)
	{
		return EBH_LevelTier::Grey;
	}
	if (Diff <= S->WhiteMaxDiff)
	{
		return EBH_LevelTier::White;
	}
	if (Diff <= S->OrangeMaxDiff)
	{
		return EBH_LevelTier::Orange;
	}
	return EBH_LevelTier::Red;
}

int32 UBH_EnemyReadabilityLibrary::GetViewerLevel(const AActor* Viewer)
{
	if (!Viewer)
	{
		return 1;
	}
	const UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(const_cast<AActor*>(Viewer));
	if (ASC && ASC->HasAttributeSetForAttribute(UAH_AttributeSet::GetLevelAttribute()))
	{
		return FMath::Max(1, FMath::RoundToInt(ASC->GetNumericAttribute(UAH_AttributeSet::GetLevelAttribute())));
	}
	return 1;
}

FLinearColor UBH_EnemyReadabilityLibrary::GetPaletteColor(FName Token)
{
	const UBH_EnemyReadabilitySettings* S = UBH_EnemyReadabilitySettings::Get();
	UObject* Palette = S->PaletteAsset.IsNull() ? nullptr : S->PaletteAsset.LoadSynchronous();
	if (!Palette || Token.IsNone())
	{
		return FLinearColor::White;
	}
	const FString Wanted = Token.ToString();
	for (TFieldIterator<FStructProperty> It(Palette->GetClass()); It; ++It)
	{
		if (It->Struct == TBaseStructure<FLinearColor>::Get() && It->GetName().Equals(Wanted, ESearchCase::IgnoreCase))
		{
			return *It->ContainerPtrToValuePtr<FLinearColor>(Palette);
		}
	}
	return FLinearColor::White;
}

FLinearColor UBH_EnemyReadabilityLibrary::GetLevelColor(EBH_LevelTier Tier)
{
	const UBH_EnemyReadabilitySettings* S = UBH_EnemyReadabilitySettings::Get();
	switch (Tier)
	{
	case EBH_LevelTier::Grey: return GetPaletteColor(S->LevelGreyToken);
	case EBH_LevelTier::Orange: return GetPaletteColor(S->LevelOrangeToken);
	case EBH_LevelTier::Red: return GetPaletteColor(S->LevelRedToken);
	default: return GetPaletteColor(S->LevelWhiteToken);
	}
}

FLinearColor UBH_EnemyReadabilityLibrary::GetArchetypeTelegraphColor(EBH_EnemyArchetype Archetype)
{
	const UBH_EnemyReadabilitySettings* S = UBH_EnemyReadabilitySettings::Get();
	const FName* Token = S->TelegraphColorTokens.Find(Archetype);
	return GetPaletteColor(Token ? *Token : FName(TEXT("Crimson")));
}

UTexture2D* UBH_EnemyReadabilityLibrary::GetRoleGlyph(EBH_EnemyArchetype Archetype)
{
	const TSoftObjectPtr<UTexture2D>* Glyph = UBH_EnemyReadabilitySettings::Get()->RoleGlyphs.Find(Archetype);
	return (Glyph && !Glyph->IsNull()) ? Glyph->LoadSynchronous() : nullptr;
}

USoundBase* UBH_EnemyReadabilityLibrary::GetAggroSound(EBH_EnemyRank Rank)
{
	const TSoftObjectPtr<USoundBase>* Sound = UBH_EnemyReadabilitySettings::Get()->AggroSounds.Find(Rank);
	return (Sound && !Sound->IsNull()) ? Sound->LoadSynchronous() : nullptr;
}

USoundBase* UBH_EnemyReadabilityLibrary::GetTelegraphSound(EBH_EnemyArchetype Archetype)
{
	const TSoftObjectPtr<USoundBase>* Sound = UBH_EnemyReadabilitySettings::Get()->TelegraphSounds.Find(Archetype);
	return (Sound && !Sound->IsNull()) ? Sound->LoadSynchronous() : nullptr;
}

FBH_EnemyReadability UBH_EnemyReadabilityLibrary::Describe(const AActor* Enemy, const AActor* Viewer)
{
	FBH_EnemyReadability Out;
	if (!Enemy || !UBH_EnemyRankLibrary::HasRankData(Enemy))
	{
		return Out;
	}

	Out.bValid = true;
	Out.Rank = UBH_EnemyRankLibrary::GetRank(Enemy);
	Out.Archetype = UBH_EnemyRankLibrary::GetArchetype(Enemy);
	Out.Level = UBH_EnemyRankLibrary::GetEnemyLevel(Enemy);
	Out.bBossPresented = UBH_EnemyRankLibrary::IsBossPresented(Enemy);
	Out.LevelTier = Viewer ? GetLevelTier(Out.Level, GetViewerLevel(Viewer)) : EBH_LevelTier::White;

	// Same name order as the target panel: identity name, then the legacy ABH_EnemyBase name, then the actor name.
	const UBH_CombatIdentityComponent* Identity = UBH_CombatIdentityComponent::Find(Enemy);
	const ABH_EnemyBase* Crab = Cast<ABH_EnemyBase>(Enemy);
	Out.Name = Identity && !Identity->DisplayName.IsEmpty() ? Identity->DisplayName
		: Crab && !Crab->DisplayName.IsEmpty() ? Crab->DisplayName : FText::FromString(Enemy->GetName());

	switch (Out.Rank)
	{
	case EBH_EnemyRank::Elite:
		Out.Title = FText::Format(LOCTEXT("EliteTitle", "{0} {1}"), RankLabel(Out.Rank), ArchetypeLabel(Out.Archetype));
		break;
	case EBH_EnemyRank::Boss:
		Out.Title = RankLabel(Out.Rank);
		break;
	default:
		break;
	}
	return Out;
}

#undef LOCTEXT_NAMESPACE
