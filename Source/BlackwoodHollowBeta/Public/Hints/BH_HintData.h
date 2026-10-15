// Blackwood Hollow - first-time hint data (Phase 13E)
// Target: Unreal Engine 5.8 (C++)
//
// One UBH_HintData asset per hint, all under /Game/BlackwoodHollow/Data/Hints (an always-cook folder; the hints are found by a folder scan,
// which the cooker does not follow). The SERVER decides when a hint fires (UBH_PlayerHintComponent on the PlayerState); the owning client
// shows it through UBH_HintSubsystem and the 13C notification queue. Seen hints are kept in a local SaveGame per player profile.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DeveloperSettings.h"
#include "GameFramework/SaveGame.h"
#include "GameplayTagContainer.h"
#include "AttributeSet.h"
#include "Combat/BH_EnemyRank.h"
#include "BH_HintData.generated.h"

class UInputAction;

/** What makes a hint fire. The server evaluates all of these; none of them is decided by a client. */
UENUM(BlueprintType)
enum class EBH_HintTrigger : uint8
{
	/** Never fires by itself. Fired by key from code or Blueprint (UBH_HintLibrary::TriggerHintKey) or by an ABH_HintTrigger volume that references the asset. */
	Manual,
	/** The player's pawn is alive for the first time this session. Use DelaySeconds for a short pause. */
	PlayerReady,
	/** The player's pawn gains TriggerTag (a state tag such as State.Combat.Blocking). */
	PlayerTag,
	/** The player's Attribute (as a fraction of MaxAttribute when that is set, else the raw value) crosses Threshold. */
	Attribute,
	/** An enemy of at least MinEnemyRank starts fighting this player. */
	EnemyEngaged,
	/** Another player in the party is down and can be revived. */
	PartnerDown,
};

UCLASS(BlueprintType)
class BLACKWOODHOLLOWBETA_API UBH_HintData : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Stable id, saved in the seen list. Never rename a shipped hint. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hint")
	FName HintId;

	/** Short title, shown in Cinzel ("Parry"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hint")
	FText Title;

	/** One or two short lines. Never names a key: the glyphs below show the keys for the active device. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hint", meta = (MultiLine = true))
	FText Text;

	/** Input actions shown as device glyphs (WBP_UI_InputGlyph) beside the text. Up to 3 are shown. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hint")
	TArray<TObjectPtr<UInputAction>> GlyphActions;

	/** Hold time in seconds. 0 = UBH_HintSettings::DefaultHoldSeconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hint", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float HoldSeconds = 0.f;

	/** When several hints wait, the higher number shows first. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hint")
	int32 Priority = 0;

	/** Order in the Field Notes list (golden-path order, lowest first). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hint")
	int32 SortOrder = 0;

	/** Wait this long after the trigger before showing (client side). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trigger", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float DelaySeconds = 0.f;

	/** The hint waits until the player has seen all of these (client side). Keeps "one new thing at a time". */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trigger")
	TArray<FName> RequiresSeen;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trigger")
	EBH_HintTrigger Trigger = EBH_HintTrigger::Manual;

	/** Manual: the key code fires (UBH_HintLibrary::TriggerHintKey). Empty = the hint has no key (volumes reference the asset directly). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trigger", meta = (EditCondition = "Trigger == EBH_HintTrigger::Manual"))
	FName TriggerKey;

	/** PlayerTag: the state tag on the player's pawn that fires the hint. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trigger", meta = (EditCondition = "Trigger == EBH_HintTrigger::PlayerTag"))
	FGameplayTag TriggerTag;

	/** Attribute: the attribute watched. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trigger", meta = (EditCondition = "Trigger == EBH_HintTrigger::Attribute"))
	FGameplayAttribute Attribute;

	/** Attribute: optional maximum. When set the value is compared as Attribute / MaxAttribute (0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trigger", meta = (EditCondition = "Trigger == EBH_HintTrigger::Attribute"))
	FGameplayAttribute MaxAttribute;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trigger", meta = (EditCondition = "Trigger == EBH_HintTrigger::Attribute"))
	float Threshold = 0.f;

	/** Attribute: true = fires when the value is below Threshold (health under 60%); false = at or above it (any Blight build-up). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trigger", meta = (EditCondition = "Trigger == EBH_HintTrigger::Attribute"))
	bool bFireWhenBelow = true;

	/** EnemyEngaged: the lowest enemy rank that fires the hint. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Trigger", meta = (EditCondition = "Trigger == EBH_HintTrigger::EnemyEngaged"))
	EBH_EnemyRank MinEnemyRank = EBH_EnemyRank::Common;
};

/** The local, per-profile list of hints this player has seen. One slot per local player. */
UCLASS()
class BLACKWOODHOLLOWBETA_API UBH_HintSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TArray<FName> SeenHints;
};

/** Project Settings > Game > "Blackwood Hollow Hints". Values are recorded in claude/balance.md. */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Blackwood Hollow Hints"))
class BLACKWOODHOLLOWBETA_API UBH_HintSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UBH_HintSettings()
	{
		CategoryName = TEXT("Game");
	}

	static const UBH_HintSettings* Get() { return GetDefault<UBH_HintSettings>(); }

	UPROPERTY(Config, EditAnywhere, Category = "Hints")
	bool bHintsEnabled = true;

	/** Content folder scanned for UBH_HintData assets. Must stay inside an always-cook folder. */
	UPROPERTY(Config, EditAnywhere, Category = "Hints")
	FString HintsFolder = TEXT("/Game/BlackwoodHollow/Data/Hints");

	/** SaveGame slot name prefix; the local player's controller id is appended ("BH_SeenHints_0"). */
	UPROPERTY(Config, EditAnywhere, Category = "Hints")
	FString SaveSlotPrefix = TEXT("BH_SeenHints_");

	UPROPERTY(Config, EditAnywhere, Category = "Display", meta = (ClampMin = "1.0", ForceUnits = "s"))
	float DefaultHoldSeconds = 7.f;

	/** Pause between two hints, counted from the moment the first one is gone. */
	UPROPERTY(Config, EditAnywhere, Category = "Display", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float MinGapSeconds = 4.f;

	/** No hint shows (or starts) within this long of a boss (or elite) bar presenting its enemy: the title card. */
	UPROPERTY(Config, EditAnywhere, Category = "Display", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float BossIntroSeconds = 4.f;

	/** How often the server checks tag, attribute and partner triggers. */
	UPROPERTY(Config, EditAnywhere, Category = "Server", meta = (ClampMin = "0.05", ForceUnits = "s"))
	float ServerPollInterval = 0.2f;
};
