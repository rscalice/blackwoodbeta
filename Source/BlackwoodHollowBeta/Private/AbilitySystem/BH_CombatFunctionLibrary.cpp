// Blackwood Hollow - Combat setup / GASP overlay bridge function library (implementation)

#include "AbilitySystem/BH_CombatFunctionLibrary.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystem/Abilities/AH_GA_Block.h"
#include "AbilitySystem/Abilities/AH_GA_Dodge.h"
#include "AbilitySystem/Abilities/AH_GA_HitReaction.h"
#include "AbilitySystem/Abilities/AH_GA_PostureBreak.h"
#include "Combat/BH_CombatFeel.h"
#include "UI/BH_HUDElements.h"
#include "Combat/BH_WeaponLoadoutDataAsset.h"
#include "Combat/BH_WeaponBladeData.h"
#include "Combat/BH_StanceWatcherComponent.h"
#include "Combat/BH_CombatIdentityComponent.h"
#include "Components/BPC_HeartFragment.h"
#include "Combat/BH_LoadoutComponent.h"
#include "Characters/BH_EnemyBase.h"
#include "Characters/BH_CharacterBase.h"
#include "Combat/BH_StanceComponent.h"
#include "Engine/Texture2D.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Abilities/GameplayAbility.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Combat/BH_TwoHandAimComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "HAL/IConsoleManager.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimNode_LinkedAnimGraph.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/World.h"
#include "Animation/AnimInstance.h"
#include "Animation/Skeleton.h"
#include "AbilitySystem/Effects/AH_GE_CombatEffects.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectIterator.h"
#include "UI/BH_HUDWidget.h"
#include "UI/BH_HUDSubsystem.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

namespace BH_CombatFunctionLibrary_Private
{
	// Re-entrancy guard for the Handle*Input entry points (game thread only). A nested call (e.g. editor Python running
	// under FEditorScriptExecutionGuard, which executes RPCs locally and can recurse ASC->TryActivateAbility ->
	// ServerTryActivateAbility -> TryActivateAbility ...) is rejected instead of recursing until the stack dies.
	// Function-local static (not file-scope) to stay safe under Live Coding patches.
	static bool& InputInFlight() { static bool bInFlight = false; return bInFlight; }
	struct FInputReentrancyGuard
	{
		bool bNested;
		FInputReentrancyGuard() : bNested(InputInFlight()) { InputInFlight() = true; }
		~FInputReentrancyGuard() { if (!bNested) { InputInFlight() = false; } }
	};
#define BH_REJECT_NESTED_INPUT(Guard, FuncName) \
	if ((Guard).bNested) \
	{ \
		UE_LOG(LogBHCombat, Verbose, TEXT(FuncName ": nested call rejected (re-entrancy guard).")); \
		return false; \
	}

	// Tuning for GetSecondaryGripIKTarget (live-tunable console variables; cm unless noted).
	// Elbow hint offsets, character-relative.
	static TAutoConsoleVariable<float> CVarGripElbowLeft(TEXT("bh.GripIK.ElbowLeftCm"), 15.f, TEXT("Left-hand grip IK: elbow hint offset toward character-left (cm)."));
	static TAutoConsoleVariable<float> CVarGripElbowBack(TEXT("bh.GripIK.ElbowBackCm"), 10.f, TEXT("Left-hand grip IK: elbow hint offset behind the character (cm)."));
	static TAutoConsoleVariable<float> CVarGripElbowDown(TEXT("bh.GripIK.ElbowDownCm"), 30.f, TEXT("Left-hand grip IK: elbow hint offset downward (cm)."));
	// Reach fade: grip distance from the left shoulder / left arm length. Full IK up to Full, none from None on.
	static TAutoConsoleVariable<float> CVarGripReachFull(TEXT("bh.GripIK.ReachFull"), 1.0f, TEXT("Left-hand grip IK: reach ratio up to which the IK is fully applied."));
	static TAutoConsoleVariable<float> CVarGripReachNone(TEXT("bh.GripIK.ReachNone"), 1.5f, TEXT("Left-hand grip IK: reach ratio from which the IK is fully faded out."));
	// Grip slide toward the right hand when out of reach: max distance in weapon-local units, and search steps.
	static TAutoConsoleVariable<float> CVarGripSlideMax(TEXT("bh.GripIK.SlideMaxLocal"), 10.f, TEXT("Left-hand grip IK: max slide of the grip toward the right hand (weapon-local units)."));
	static constexpr int32 BH_GripSlideSteps = 20;
	// Two-hand weapon grip frame (ApplyTwoHandAim): the weapon is posed from the two hands of the visible mesh, no angle clamp.
	static TAutoConsoleVariable<float> CVarGripFrameWeight(TEXT("bh.GripIK.FrameWeight"), 1.0f, TEXT("Two-hand grip frame: overall weight (0 = authored attach only, 1 = weapon follows both hands)."));
	static TAutoConsoleVariable<float> CVarGripMinHandGap(TEXT("bh.GripIK.MinHandGap"), 5.0f, TEXT("Two-hand grip frame: below this hand-to-hand distance (cm) the weapon falls back to the authored attach."));
	static TAutoConsoleVariable<float> CVarGripMaxHandGap(TEXT("bh.GripIK.MaxHandGap"), 45.0f, TEXT("Two-hand grip frame: above this hand-to-hand distance (cm) the weapon falls back to the authored attach."));
	static TAutoConsoleVariable<float> CVarGripPalmFrac(TEXT("bh.GripIK.PalmFrac"), 0.5f, TEXT("Two-hand grip frame: left palm point = hand_l lerped toward middle_01_l by this fraction."));
	static TAutoConsoleVariable<float> CVarGripFrameBlendRate(TEXT("bh.GripIK.FrameBlendRate"), 12.0f, TEXT("Two-hand grip frame: weight interpolation speed (1/s) when entering / leaving the fallback."));
	static TAutoConsoleVariable<float> CVarGripMaxStepDegPerSec(TEXT("bh.GripIK.MaxStepDegPerSec"), 3300.0f, TEXT("Two-hand grip frame: max weapon rotation speed (degrees per second, frame-rate independent: 3300 = 55 deg per frame at 60 fps) - a rate limit for clip cuts / degenerate hand axes, not an angle clamp (0 = off)."));
	static TAutoConsoleVariable<int32> CVarGripUseLeftHandIK(TEXT("bh.GripIK.UseLeftHandIK"), 0, TEXT("1 = the ABP left-hand Two Bone IK is driven by GetSecondaryGripIKTarget (legacy); 0 = alpha forced to 0 (the grip frame moves the weapon to the hands instead)."));

	// Component tags used to find/clean up the weapon meshes this library spawns.
	// (Plain functions returning FName rather than file-scope or function-local statics, to stay
	// safe under Live Coding patches: a hot-reloaded static can be left default-constructed.)
	static FName WeaponComponentTag() { return FName(TEXT("BH.Weapon")); }
	static FName WeaponSlotTag(EBH_WeaponSlot Slot)
	{
		return Slot == EBH_WeaponSlot::OffHand ? FName(TEXT("BH.Weapon.OffHand")) : FName(TEXT("BH.Weapon.MainHand"));
	}

	static UAbilitySystemComponent* ResolveAbilitySystemComponent(AActor* OwningActor)
	{
		if (!OwningActor)
		{
			return nullptr;
		}

		if (UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(OwningActor))
		{
			return ASC;
		}

		// Fall back to a plain component search in case OwningActor doesn't
		// implement IAbilitySystemInterface (e.g. a Blueprint-only character
		// that just has the component added without wiring the interface).
		return OwningActor->FindComponentByClass<UAbilitySystemComponent>();
	}
}

// ============================================================================
// Setup / abilities
// ============================================================================

static TAutoConsoleVariable<int32> CVarBHCapsuleBlocksPawns(
	TEXT("bh.Combat.CapsuleBlocksPawns"), 1,
	TEXT("1 = SetupCombatCharacter makes the character's capsule Block the Pawn channel (GASP's capsule profile ignores it, so pawns would overlap). 0 = leave the profile alone."),
	ECVF_Default);

bool UBH_CombatFunctionLibrary::SetupCombatCharacter(AActor* OwningActor, TSubclassOf<UGameplayAbility> OverloadBurstAbilityClass)
{
	using namespace BH_CombatFunctionLibrary_Private;

	UAbilitySystemComponent* ASC = ResolveAbilitySystemComponent(OwningActor);
	if (!ASC)
	{
		UE_LOG(LogTemp, Warning, TEXT("SetupCombatCharacter: no AbilitySystemComponent found on '%s'."), OwningActor ? *OwningActor->GetName() : TEXT("None"));
		return false;
	}

	ASC->InitAbilityActorInfo(OwningActor, OwningActor);

	// Pawns must collide with each other (melee hitboxes are object-type sweeps, so they are unaffected).
	if (CVarBHCapsuleBlocksPawns.GetValueOnGameThread() != 0)
	{
		if (UCapsuleComponent* Capsule = OwningActor->FindComponentByClass<UCapsuleComponent>())
		{
			Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		}
	}

	if (!ASC->GetSet<UAH_AttributeSet>())
	{
		UAH_AttributeSet* NewAttributeSet = NewObject<UAH_AttributeSet>(OwningActor, UAH_AttributeSet::StaticClass(), TEXT("AH_AttributeSet"));
		ASC->AddAttributeSetSubobject(NewAttributeSet);
	}

	// Passive posture / stamina regeneration (server-side periodic GEs).
	ApplyPassiveRegenEffects(OwningActor);

	// OverloadBurstAbilityClass is deprecated/ignored: the Heart-Fragment loadout grants Overload Burst now.
	if (OwningActor->HasAuthority())
	{
		if (UBPC_HeartFragment* HeartFragment = OwningActor->FindComponentByClass<UBPC_HeartFragment>())
		{
			HeartFragment->GrantEquippedFragments();
		}
	}

	// Phase 8C: player pawns get the weapon-loadout component (creates the Narrative equipment component, derives
	// the available stances, applies equipment stat mods). Enemies use ABH_EnemyBase and never get one; a Blueprint
	// component of the same class on the character wins (no duplicate is added).
	if (APawn* Pawn = Cast<APawn>(OwningActor))
	{
		if (!Cast<ABH_EnemyBase>(Pawn) && !UBH_CombatIdentityComponent::Find(Pawn) && !Pawn->FindComponentByClass<UBH_LoadoutComponent>())
		{
			UBH_LoadoutComponent* Loadout = NewObject<UBH_LoadoutComponent>(Pawn, TEXT("Loadout"));
			Pawn->AddInstanceComponent(Loadout);
			Loadout->RegisterComponent();
		}
	}

	return true;
}

bool UBH_CombatFunctionLibrary::HandleFragmentInput(AActor* OwningActor, int32 Slot)
{
	using namespace BH_CombatFunctionLibrary_Private;

	const FInputReentrancyGuard Guard;
	BH_REJECT_NESTED_INPUT(Guard, "HandleFragmentInput")
	if (UBPC_HeartFragment* HeartFragment = OwningActor ? OwningActor->FindComponentByClass<UBPC_HeartFragment>() : nullptr)
	{
		return HeartFragment->TryActivateFragment(Slot);
	}
	return false;
}

UTexture2D* UBH_CombatFunctionLibrary::GetStanceIconForPose(const AActor* Character, FName PoseDisplayName)
{
	if (!Character || PoseDisplayName.IsNone())
	{
		return nullptr;
	}

	const UBH_WeaponLoadoutDataAsset* Loadouts = nullptr;
	if (const UBH_StanceComponent* Stance = UBH_StanceComponent::FindStanceComponent(Character))
	{
		Loadouts = Stance->WeaponLoadouts;
	}
	else if (const FObjectProperty* Property = CastField<FObjectProperty>(Character->GetClass()->FindPropertyByName(FName(TEXT("WeaponLoadouts")))))
	{
		Loadouts = Cast<UBH_WeaponLoadoutDataAsset>(Property->GetObjectPropertyValue_InContainer(Character));
	}
	if (!Loadouts)
	{
		if (const UBH_StanceWatcherComponent* Watcher = UBH_StanceWatcherComponent::FindStanceWatcher(Character))
		{
			Loadouts = Watcher->FallbackLoadouts;
		}
	}

	FBH_OverlayWeaponLoadout Loadout;
	if (Loadouts && Loadouts->FindLoadout(PoseDisplayName, Loadout))
	{
		return Loadout.StanceIcon;
	}
	return nullptr;
}

TSubclassOf<UGameplayAbility> UBH_CombatFunctionLibrary::GetMeleeAbilityForPose(const AActor* Character, FName PoseDisplayName)
{
	if (!Character || PoseDisplayName.IsNone())
	{
		return nullptr;
	}

	const UBH_WeaponLoadoutDataAsset* Loadouts = nullptr;
	if (const UBH_StanceComponent* Stance = UBH_StanceComponent::FindStanceComponent(Character))
	{
		Loadouts = Stance->WeaponLoadouts;
	}
	else if (const FObjectProperty* Property = CastField<FObjectProperty>(Character->GetClass()->FindPropertyByName(FName(TEXT("WeaponLoadouts")))))
	{
		Loadouts = Cast<UBH_WeaponLoadoutDataAsset>(Property->GetObjectPropertyValue_InContainer(Character));
	}
	if (!Loadouts)
	{
		if (const UBH_StanceWatcherComponent* Watcher = UBH_StanceWatcherComponent::FindStanceWatcher(Character))
		{
			Loadouts = Watcher->FallbackLoadouts;
		}
	}

	FBH_OverlayWeaponLoadout Loadout;
	if (Loadouts && Loadouts->FindLoadout(PoseDisplayName, Loadout))
	{
		return Loadout.MeleeAbility;
	}
	return nullptr;
}

void UBH_CombatFunctionLibrary::GrantCombatAbilities(AActor* OwningActor, const TArray<TSubclassOf<UGameplayAbility>>& AbilityClasses)
{
	using namespace BH_CombatFunctionLibrary_Private;

	if (!OwningActor || !OwningActor->HasAuthority())
	{
		return;
	}

	UAbilitySystemComponent* ASC = ResolveAbilitySystemComponent(OwningActor);
	if (!ASC)
	{
		UE_LOG(LogTemp, Warning, TEXT("GrantCombatAbilities: no AbilitySystemComponent on '%s'."), *OwningActor->GetName());
		return;
	}

	for (const TSubclassOf<UGameplayAbility>& AbilityClass : AbilityClasses)
	{
		if (AbilityClass && !ASC->FindAbilitySpecFromClass(AbilityClass))
		{
			ASC->GiveAbility(FGameplayAbilitySpec(AbilityClass, 1, INDEX_NONE, OwningActor));
		}
	}
}

bool UBH_CombatFunctionLibrary::HandleMeleeAttackInput(AActor* OwningActor, TSubclassOf<UGameplayAbility> MeleeAbilityClass)
{
	using namespace BH_CombatFunctionLibrary_Private;

	const FInputReentrancyGuard Guard;
	BH_REJECT_NESTED_INPUT(Guard, "HandleMeleeAttackInput")

	UAbilitySystemComponent* ASC = ResolveAbilitySystemComponent(OwningActor);
	if (!ASC || !MeleeAbilityClass)
	{
		return false;
	}

	const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromClass(MeleeAbilityClass);
	if (!Spec)
	{
		UE_LOG(LogTemp, Warning, TEXT("HandleMeleeAttackInput: '%s' has not been granted %s."), *OwningActor->GetName(), *MeleeAbilityClass->GetName());
		return false;
	}

	if (Spec->IsActive())
	{
		// Already mid-attack: hand the press to the running ability as a buffered combo input.
		FGameplayEventData Payload;
		Payload.EventTag = TAG_Event_Combat_Input_Attack;
		Payload.Instigator = OwningActor;
		Payload.Target = OwningActor;
		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(OwningActor, TAG_Event_Combat_Input_Attack, Payload);
		return true;
	}

	return ASC->TryActivateAbility(Spec->Handle);
}

bool UBH_CombatFunctionLibrary::HandleBlockInput(AActor* OwningActor, TSubclassOf<UGameplayAbility> BlockAbilityClass, bool bPressed)
{
	using namespace BH_CombatFunctionLibrary_Private;

	const FInputReentrancyGuard Guard;
	BH_REJECT_NESTED_INPUT(Guard, "HandleBlockInput")

	UAbilitySystemComponent* ASC = ResolveAbilitySystemComponent(OwningActor);
	if (!ASC || !BlockAbilityClass)
	{
		return false;
	}

	const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromClass(BlockAbilityClass);
	if (!Spec)
	{
		UE_LOG(LogTemp, Warning, TEXT("HandleBlockInput: '%s' has not been granted %s."), *OwningActor->GetName(), *BlockAbilityClass->GetName());
		return false;
	}

	if (bPressed)
	{
		return Spec->IsActive() || ASC->TryActivateAbility(Spec->Handle);
	}

	if (Spec->IsActive())
	{
		// End (rather than cancel) a block so its "End" (lower guard) section can
		// play: cancelling makes PlayMontageAndWait stop the montage immediately.
		UAH_GA_Block* ActiveBlock = UAH_GA_Block::FindActiveBlock(ASC);
		if (ActiveBlock && ActiveBlock->GetClass()->IsChildOf(BlockAbilityClass) && ActiveBlock->IsActive())
		{
			ActiveBlock->EndAbility(ActiveBlock->GetCurrentAbilitySpecHandle(), ActiveBlock->GetCurrentActorInfo(),
				ActiveBlock->GetCurrentActivationInfo(), /*bReplicateEndAbility*/ true, /*bWasCancelled*/ false);
		}
		else
		{
			ASC->CancelAbilityHandle(Spec->Handle);
		}
	}
	return true;
}

bool UBH_CombatFunctionLibrary::HandleDodgeInput(AActor* OwningActor)
{
	using namespace BH_CombatFunctionLibrary_Private;

	const FInputReentrancyGuard Guard;
	BH_REJECT_NESTED_INPUT(Guard, "HandleDodgeInput")

	UAbilitySystemComponent* ASC = ResolveAbilitySystemComponent(OwningActor);
	if (!ASC)
	{
		return false;
	}

	FGameplayTagContainer DodgeTags;
	DodgeTags.AddTag(TAG_Ability_Combat_Dodge);
	return ASC->TryActivateAbilitiesByTag(DodgeTags);
}

bool UBH_CombatFunctionLibrary::HandleParryInput(AActor* OwningActor)
{
	using namespace BH_CombatFunctionLibrary_Private;

	const FInputReentrancyGuard Guard;
	BH_REJECT_NESTED_INPUT(Guard, "HandleParryInput")

	UAbilitySystemComponent* ASC = ResolveAbilitySystemComponent(OwningActor);
	if (!ASC)
	{
		return false;
	}

	FGameplayTagContainer ParryTags;
	ParryTags.AddTag(TAG_Ability_Combat_Parry);
	return ASC->TryActivateAbilitiesByTag(ParryTags);
}

bool UBH_CombatFunctionLibrary::CheckStaminaCost(const UAbilitySystemComponent* ASC, float Cost, bool bAllowOvercommit)
{
	if (Cost <= 0.f)
	{
		return true;
	}
	if (!ASC || !ASC->HasAttributeSetForAttribute(UAH_AttributeSet::GetStaminaAttribute()))
	{
		// No stamina attribute on this actor (e.g. a prop): nothing to spend.
		return true;
	}

	const float Stamina = ASC->GetNumericAttribute(UAH_AttributeSet::GetStaminaAttribute());
	return bAllowOvercommit ? Stamina > 0.f : Stamina >= Cost;
}

bool UBH_CombatFunctionLibrary::ApplyStaminaCost(UAbilitySystemComponent* ASC, float Cost)
{
	if (!ASC || Cost <= 0.f || !ASC->HasAttributeSetForAttribute(UAH_AttributeSet::GetStaminaAttribute()))
	{
		return false;
	}

	FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
	const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(UAH_GE_StaminaCost::StaticClass(), 1.f, Context);
	if (!Spec.IsValid())
	{
		return false;
	}

	Spec.Data->SetSetByCallerMagnitude(TAG_Data_StaminaCost, Cost);
	// Instant effects never return an active handle, so there is nothing to test on the result.
	ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
	UE_LOG(LogBHCombat, Verbose, TEXT("ApplyStaminaCost: %s spent %.1f stamina (now %.1f)."), *GetNameSafe(ASC->GetOwner()), Cost,
		ASC->GetNumericAttribute(UAH_AttributeSet::GetStaminaAttribute()));
	return true;
}

void UBH_CombatFunctionLibrary::ApplyPassiveRegenEffects(AActor* OwningActor)
{
	using namespace BH_CombatFunctionLibrary_Private;

	if (!OwningActor || !OwningActor->HasAuthority())
	{
		return;
	}

	UAbilitySystemComponent* ASC = ResolveAbilitySystemComponent(OwningActor);
	if (!ASC || !ASC->GetSet<UAH_AttributeSet>())
	{
		return;
	}

	const TSubclassOf<UGameplayEffect> RegenEffects[] = { UAH_GE_PostureRegen::StaticClass(), UAH_GE_StaminaRegen::StaticClass() };
	for (const TSubclassOf<UGameplayEffect>& EffectClass : RegenEffects)
	{
		FGameplayEffectQuery AlreadyActive;
		AlreadyActive.EffectDefinition = EffectClass;
		if (ASC->GetActiveEffects(AlreadyActive).Num() > 0)
		{
			continue;
		}

		FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
		Context.AddSourceObject(OwningActor);
		const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(EffectClass, 1.f, Context);
		if (Spec.IsValid())
		{
			ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
		}
	}
}

// ============================================================================
// HUD
// ============================================================================

namespace BH_CombatFunctionLibrary_Private
{
	static bool TrySetupPlayerHUDNow(APawn* Pawn, TSubclassOf<UBH_HUDWidget> MainHUDClass, TSubclassOf<UUserWidget> DebugHUDClass)
	{
		if (!Pawn || !Pawn->IsLocallyControlled())
		{
			return false;
		}
		const APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
		ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
		UBH_HUDSubsystem* HUD = LocalPlayer ? LocalPlayer->GetSubsystem<UBH_HUDSubsystem>() : nullptr;
		return HUD && HUD->SetupHUD(Pawn, MainHUDClass, DebugHUDClass);
	}
}

void UBH_CombatFunctionLibrary::SetupPlayerHUD(APawn* Pawn, TSubclassOf<UBH_HUDWidget> MainHUDClass, TSubclassOf<UUserWidget> DebugHUDClass)
{
	using namespace BH_CombatFunctionLibrary_Private;

	UWorld* World = Pawn ? Pawn->GetWorld() : nullptr;
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (TrySetupPlayerHUDNow(Pawn, MainHUDClass, DebugHUDClass))
	{
		return;
	}

	// BeginPlay can run before possession (and on clients before the controller replicates):
	// retry for up to 5 s. Pawns that are never locally controlled here simply time out.
	TSharedRef<FTimerHandle> Handle = MakeShared<FTimerHandle>();
	TSharedRef<int32> Attempts = MakeShared<int32>(0);
	TWeakObjectPtr<APawn> WeakPawn(Pawn);
	TWeakObjectPtr<UWorld> WeakWorld(World);
	World->GetTimerManager().SetTimer(*Handle, FTimerDelegate::CreateLambda(
		[WeakPawn, WeakWorld, MainHUDClass, DebugHUDClass, Handle, Attempts]()
		{
			UWorld* TimerWorld = WeakWorld.Get();
			APawn* TimerPawn = WeakPawn.Get();
			const bool bDone = !TimerPawn || TrySetupPlayerHUDNow(TimerPawn, MainHUDClass, DebugHUDClass) || ++(*Attempts) >= 25;
			if (bDone && TimerWorld)
			{
				TimerWorld->GetTimerManager().ClearTimer(*Handle);
			}
		}), 0.2f, true);
}

void UBH_CombatFunctionLibrary::ToggleDebugHUD(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	if (!GameInstance)
	{
		return;
	}
	for (ULocalPlayer* LocalPlayer : GameInstance->GetLocalPlayers())
	{
		if (UBH_HUDSubsystem* HUD = LocalPlayer ? LocalPlayer->GetSubsystem<UBH_HUDSubsystem>() : nullptr)
		{
			HUD->ToggleDebugHUD();
		}
	}
}

// ============================================================================
// Teams / friendly fire
// ============================================================================

FGenericTeamId UBH_CombatFunctionLibrary::GetCombatTeamId(const AActor* Actor)
{
	if (!Actor)
	{
		return FGenericTeamId::NoTeam;
	}

	// Projectiles / traps / spawned hitboxes fight for whoever instigated them.
	const AActor* TeamSource = Actor;
	if (!Actor->IsA<APawn>())
	{
		if (const APawn* InstigatorPawn = Actor->GetInstigator())
		{
			TeamSource = InstigatorPawn;
		}
	}

	// 1) Explicit team on the actor (ABH_EnemyBase implements IGenericTeamAgentInterface).
	if (const IGenericTeamAgentInterface* Agent = Cast<const IGenericTeamAgentInterface>(TeamSource))
	{
		const FGenericTeamId TeamId = Agent->GetGenericTeamId();
		if (TeamId != FGenericTeamId::NoTeam)
		{
			return TeamId;
		}
	}

	// 1b) Replicated team on a UBH_CombatIdentityComponent (AI characters built on a Blueprint-only base).
	//     Unlike the controller (server only), this resolves identically on every machine.
	if (const UBH_CombatIdentityComponent* Identity = UBH_CombatIdentityComponent::Find(TeamSource))
	{
		const FGenericTeamId TeamId = BH_CombatTeam::ToGenericTeamId(Identity->CombatTeam);
		if (TeamId != FGenericTeamId::NoTeam)
		{
			return TeamId;
		}
	}

	if (const APawn* Pawn = Cast<APawn>(TeamSource))
	{
		// 2) Explicit team on the controller (AAIController implements the interface).
		//    Controllers only exist on the server / owning client, hence step 3.
		if (const IGenericTeamAgentInterface* ControllerAgent = Cast<const IGenericTeamAgentInterface>(Pawn->GetController()))
		{
			const FGenericTeamId TeamId = ControllerAgent->GetGenericTeamId();
			if (TeamId != FGenericTeamId::NoTeam)
			{
				return TeamId;
			}
		}

		// 3) Human-controlled pawn. PlayerState replicates to everyone, so this resolves the
		//    same way on the server, the owning client and other clients.
		const APlayerState* PlayerState = Pawn->GetPlayerState();
		if (PlayerState && !PlayerState->IsABot())
		{
			return BH_CombatTeam::ToGenericTeamId(EBH_CombatTeam::Players);
		}
	}

	// 4) Unaffiliated: hittable by everyone.
	return FGenericTeamId::NoTeam;
}

EBH_CombatTeam UBH_CombatFunctionLibrary::GetCombatTeam(const AActor* Actor)
{
	return BH_CombatTeam::FromGenericTeamId(GetCombatTeamId(Actor));
}

bool UBH_CombatFunctionLibrary::AreCombatAllies(const AActor* A, const AActor* B)
{
	if (!A || !B)
	{
		return false;
	}
	const FGenericTeamId TeamA = GetCombatTeamId(A);
	return TeamA != FGenericTeamId::NoTeam && TeamA == GetCombatTeamId(B);
}

// ============================================================================
// GASP OverlayPose bridge
// ============================================================================

// DEPRECATED shim. Every live character owns a UBH_StanceComponent, so the legacy name is simply mapped to its tag
// (no warning on that path: CombatIdentity / the watcher still route through here). The old body wrote the
// replicated "OverlayPose" byte on CBP_SandboxCharacter and called UpdateOverlayPose via reflection; that was removed
// with the GASPALS plugin dependency, so a character without a stance component can no longer be driven from here.
bool UBH_CombatFunctionLibrary::ApplyOverlayPoseByDisplayName(AActor* TargetCharacter, const FString& OverlayPoseDisplayName)
{
	if (!TargetCharacter)
	{
		return false;
	}
	if (UBH_StanceComponent* Stance = UBH_StanceComponent::FindStanceComponent(TargetCharacter))
	{
		return TargetCharacter->HasAuthority() && Stance->SetStance(BH_Stance::FromLegacyName(FName(*OverlayPoseDisplayName)));
	}

	UE_LOG(LogTemp, Warning, TEXT("ApplyOverlayPoseByDisplayName is deprecated and does nothing: '%s' has no UBH_StanceComponent (requested '%s'). Use the stance component's SetStance / RequestStance."),
		*TargetCharacter->GetName(), *OverlayPoseDisplayName);
	return false;
}

int32 UBH_CombatFunctionLibrary::GetNextStanceIndex(const AActor* TargetCharacter, const TArray<FString>& StanceCycle)
{
	if (StanceCycle.Num() == 0)
	{
		return INDEX_NONE;
	}
	const FString Current = GetCurrentOverlayPoseDisplayName(TargetCharacter);
	for (int32 Index = 0; Index < StanceCycle.Num(); ++Index)
	{
		if (StanceCycle[Index].Equals(Current, ESearchCase::IgnoreCase))
		{
			return (Index + 1) % StanceCycle.Num();
		}
	}
	return 0;
}

bool UBH_CombatFunctionLibrary::RequestStanceByName(AActor* TargetCharacter, const FString& OverlayPoseDisplayName)
{
	if (!TargetCharacter)
	{
		return false;
	}
	if (UBH_StanceComponent* Stance = UBH_StanceComponent::FindStanceComponent(TargetCharacter))
	{
		return Stance->RequestStanceByLegacyName(FName(*OverlayPoseDisplayName));
	}
	if (UBH_StanceWatcherComponent* Watcher = UBH_StanceWatcherComponent::FindStanceWatcher(TargetCharacter))
	{
		Watcher->RequestStance(OverlayPoseDisplayName);
		return true;
	}
	// No watcher on this character: only the authority can apply the pose.
	return TargetCharacter->HasAuthority() && ApplyOverlayPoseByDisplayName(TargetCharacter, OverlayPoseDisplayName);
}

FString UBH_CombatFunctionLibrary::GetCurrentOverlayPoseDisplayName(const AActor* TargetCharacter)
{
	if (const UBH_StanceComponent* Stance = UBH_StanceComponent::FindStanceComponent(TargetCharacter))
	{
		return Stance->GetCurrentStanceLegacyName().ToString();
	}
	// No stance component: the GASP "OverlayPose" fallback read was removed with the GASPALS dependency. Empty = unknown
	// (not warned: the HUD and watcher poll this several times a second).
	return FString();
}

// ============================================================================
// Weapon mesh attachment
// ============================================================================

FName UBH_CombatFunctionLibrary::GetWeaponSocketForSlot(EBH_WeaponSlot Slot)
{
	return Slot == EBH_WeaponSlot::OffHand ? FName(TEXT("shield_l_socket")) : FName(TEXT("weapon_r_socket"));
}

USkeletalMeshComponent* UBH_CombatFunctionLibrary::FindWeaponAttachMesh(ACharacter* Character, FName SocketName)
{
	using namespace BH_CombatFunctionLibrary_Private;

	if (!Character)
	{
		return nullptr;
	}

	TArray<USkeletalMeshComponent*> SkeletalMeshes;
	Character->GetComponents<USkeletalMeshComponent>(SkeletalMeshes);

	USkeletalMeshComponent* AnyWithSocket = nullptr;
	for (USkeletalMeshComponent* Candidate : SkeletalMeshes)
	{
		// Never attach weapons to other weapons (skeletal weapon meshes we spawned).
		if (!Candidate || Candidate->ComponentHasTag(WeaponComponentTag()) || !Candidate->DoesSocketExist(SocketName))
		{
			continue;
		}
		if (Candidate->IsVisible())
		{
			return Candidate;
		}
		if (!AnyWithSocket)
		{
			AnyWithSocket = Candidate;
		}
	}

	return AnyWithSocket ? AnyWithSocket : Character->GetMesh();
}

UMeshComponent* UBH_CombatFunctionLibrary::GetEquippedWeaponComponent(const AActor* Character, EBH_WeaponSlot Slot)
{
	using namespace BH_CombatFunctionLibrary_Private;

	if (!Character)
	{
		return nullptr;
	}

	TArray<UMeshComponent*> MeshComponents;
	Character->GetComponents<UMeshComponent>(MeshComponents);
	for (UMeshComponent* Component : MeshComponents)
	{
		if (Component && Component->ComponentHasTag(WeaponSlotTag(Slot)))
		{
			return Component;
		}
	}
	return nullptr;
}

void UBH_CombatFunctionLibrary::GetSecondaryGripIKTarget(ACharacter* Character, USkeletalMeshComponent* AnimMesh, FVector& OutCS_Target, FVector& OutCS_JointTarget, FVector& OutHandR_Offset, float& OutAlpha)
{
	using namespace BH_CombatFunctionLibrary_Private;

	OutCS_Target = FVector::ZeroVector;
	OutCS_JointTarget = FVector::ZeroVector;
	OutHandR_Offset = FVector::ZeroVector;
	OutAlpha = 0.f;

	// The two-hand grip frame (ApplyTwoHandAim) poses the weapon from both hands; the left-hand IK stays off unless asked for.
	if (CVarGripUseLeftHandIK.GetValueOnGameThread() == 0)
	{
		return;
	}

	if (!Character || !AnimMesh)
	{
		return;
	}

	UMeshComponent* Weapon = GetEquippedWeaponComponent(Character, EBH_WeaponSlot::MainHand);
	const UBH_WeaponBladeData* Data = Weapon ? Weapon->GetAssetUserData<UBH_WeaponBladeData>() : nullptr;
	if (!Data || !Data->bTwoHandedGrip)
	{
		return;
	}

	// No IK while ragdolling or airborne (the swing poses there are not authored for a fixed grip).
	if (AnimMesh->IsSimulatingPhysics())
	{
		return;
	}
	if (const UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
	{
		if (Movement->IsFalling())
		{
			return;
		}
	}

	const FTransform& MeshTransform = AnimMesh->GetComponentTransform();
	const FTransform WeaponTransform = Weapon->GetComponentTransform();

	// Elbow hint: the current lowerarm_l pushed away from the body (character-left, back and down), in cm.
	static const FName ElbowBone(TEXT("lowerarm_l"));
	static const FName UpperArmBone(TEXT("upperarm_l"));
	static const FName HandLBone(TEXT("hand_l"));
	static const FName HandRBone(TEXT("hand_r"));
	if (AnimMesh->GetBoneIndex(ElbowBone) == INDEX_NONE || AnimMesh->GetBoneIndex(UpperArmBone) == INDEX_NONE
		|| AnimMesh->GetBoneIndex(HandLBone) == INDEX_NONE || AnimMesh->GetBoneIndex(HandRBone) == INDEX_NONE)
	{
		return;
	}
	const FVector ShoulderCS = AnimMesh->GetSocketTransform(UpperArmBone, RTS_Component).GetLocation();
	const FVector ElbowCS = AnimMesh->GetSocketTransform(ElbowBone, RTS_Component).GetLocation();
	const FVector HandCS = AnimMesh->GetSocketTransform(HandLBone, RTS_Component).GetLocation();
	const float LimbLength = FVector::Dist(ShoulderCS, ElbowCS) + FVector::Dist(ElbowCS, HandCS);
	if (LimbLength < KINDA_SMALL_NUMBER)
	{
		return;
	}

	// Grip point: the authored secondary grip, slid up the handle toward the right hand only as far as needed to stay
	// inside the left arm's reach (authored swings extend the weapon; real two-handed swings bring the hands together).
	FVector GripLocal = Data->SecondaryGripLocal;
	FVector GripWorld = WeaponTransform.TransformPosition(GripLocal);
	float ReachRatio = FVector::Dist(ShoulderCS, MeshTransform.InverseTransformPosition(GripWorld)) / LimbLength;
	float BestRatio = ReachRatio;
	FVector BestLocal = GripLocal;
	for (int32 Step = 1; Step <= BH_GripSlideSteps && ReachRatio > CVarGripReachFull.GetValueOnGameThread(); ++Step)
	{
		const FVector CandLocal = Data->SecondaryGripLocal + FVector(0.f, 0.f, CVarGripSlideMax.GetValueOnGameThread() * Step / BH_GripSlideSteps);
		const FVector CandWorld = WeaponTransform.TransformPosition(CandLocal);
		ReachRatio = FVector::Dist(ShoulderCS, MeshTransform.InverseTransformPosition(CandWorld)) / LimbLength;
		if (ReachRatio < BestRatio)
		{
			BestRatio = ReachRatio;
			BestLocal = CandLocal;
		}
	}
	GripWorld = WeaponTransform.TransformPosition(BestLocal);
	OutCS_Target = MeshTransform.InverseTransformPosition(GripWorld);

	const FTransform ActorTransform = Character->GetActorTransform();
	const FVector OffsetWorld = ActorTransform.GetUnitAxis(EAxis::Y) * -CVarGripElbowLeft.GetValueOnGameThread()
		+ ActorTransform.GetUnitAxis(EAxis::X) * -CVarGripElbowBack.GetValueOnGameThread()
		+ FVector::UpVector * -CVarGripElbowDown.GetValueOnGameThread();
	OutCS_JointTarget = ElbowCS + MeshTransform.InverseTransformVectorNoScale(OffsetWorld);

	// Grip point in the right hand bone's local space (weapon and bone are from the same, last-evaluated pose).
	OutHandR_Offset = AnimMesh->GetSocketTransform(HandRBone, RTS_World).InverseTransformPosition(GripWorld);

	// Reach fade: if even the slid grip is out of reach, fade the IK out rather than stretch a straight arm at it.
	OutAlpha = 1.f - FMath::SmoothStep(CVarGripReachFull.GetValueOnGameThread(), CVarGripReachNone.GetValueOnGameThread(), BestRatio);
}

void UBH_CombatFunctionLibrary::ApplyTwoHandAim(ACharacter* Character, float DeltaSeconds, float& InOutWeight, FQuat& InOutPrevRotation, bool& bInOutHasPrevRotation)
{
	using namespace BH_CombatFunctionLibrary_Private;

	if (!Character)
	{
		InOutWeight = 0.f;
		bInOutHasPrevRotation = false;
		return;
	}

	UMeshComponent* Weapon = GetEquippedWeaponComponent(Character, EBH_WeaponSlot::MainHand);
	const UBH_WeaponBladeData* Data = Weapon ? Weapon->GetAssetUserData<UBH_WeaponBladeData>() : nullptr;
	USkeletalMeshComponent* AnimMesh = Character->GetMesh();
	USkeletalMeshComponent* Parent = Weapon ? Cast<USkeletalMeshComponent>(Weapon->GetAttachParent()) : nullptr;
	if (!Data || !Data->bTwoHandedGrip || !Parent || Data->bSheathedOnBack)
	{
		InOutWeight = 0.f;
		bInOutHasPrevRotation = false;
		return;
	}

	// The authored one-handed attach (weapon socket of the VISIBLE mesh) and the right-hand grip point it implies.
	const FTransform ParentSocket = Parent->GetSocketTransform(Weapon->GetAttachSocketName());
	const FTransform Authored = Data->AuthoredRelative * ParentSocket;
	const FVector Scale = Authored.GetScale3D();
	const FVector PrimaryWorld = Authored.TransformPosition(Data->PrimaryGripLocal);

	// Left palm point on the visible mesh: hand_l lerped toward the middle finger base.
	static const FName HandLBone(TEXT("hand_l"));
	static const FName PalmBone(TEXT("middle_01_l"));
	const USkeletalMeshComponent* HandMesh = Parent->GetBoneIndex(HandLBone) != INDEX_NONE ? Parent : AnimMesh;
	const FVector LocalAxis = (Data->SecondaryGripLocal - Data->PrimaryGripLocal).GetSafeNormal();
	const bool bHaveHand = HandMesh && HandMesh->GetBoneIndex(HandLBone) != INDEX_NONE && !LocalAxis.IsNearlyZero();

	FVector PalmWorld = PrimaryWorld;
	FVector Sep = FVector::ZeroVector;
	float Gap = 0.f;
	if (bHaveHand)
	{
		PalmWorld = HandMesh->GetSocketTransform(HandLBone, RTS_World).GetLocation();
		if (HandMesh->GetBoneIndex(PalmBone) != INDEX_NONE)
		{
			PalmWorld = FMath::Lerp(PalmWorld, HandMesh->GetSocketTransform(PalmBone, RTS_World).GetLocation(), CVarGripPalmFrac.GetValueOnGameThread());
		}
		Sep = PalmWorld - PrimaryWorld;
		Gap = Sep.Size();
	}

	// Weight: full while the hands are a plausible two-hand grip apart, fading to the authored attach for one-handed
	// moments; the BH_HandIK_L montage curve (default 1) lets a clip opt out.
	const bool bTwoHandPose = bHaveHand && Gap >= CVarGripMinHandGap.GetValueOnGameThread() && Gap <= CVarGripMaxHandGap.GetValueOnGameThread();
	const float CurveAlpha = AnimMesh ? GetMontageLayeringValue(AnimMesh->GetAnimInstance(), TEXT("BH_HandIK_L"), 1.f) : 1.f;
	const float TargetWeight = bTwoHandPose ? FMath::Clamp(CurveAlpha * CVarGripFrameWeight.GetValueOnGameThread(), 0.f, 1.f) : 0.f;
	InOutWeight = FMath::FInterpTo(InOutWeight, TargetWeight, DeltaSeconds, CVarGripFrameBlendRate.GetValueOnGameThread());

	if (InOutWeight < KINDA_SMALL_NUMBER || !bHaveHand)
	{
		if (!Weapon->GetRelativeTransform().Equals(Data->AuthoredRelative, 0.01f))
		{
			Weapon->SetRelativeTransform(Data->AuthoredRelative);
		}
		InOutPrevRotation = Authored.GetRotation();
		bInOutHasPrevRotation = true;
		return;
	}

	// Grip frame. The handle axis (primary -> secondary grip) points from the right hand to the left palm; the roll is the
	// authored weapon Y with the axis component removed (Gram-Schmidt). No angle clamp.
	const FVector Dir = Sep / FMath::Max(Gap, KINDA_SMALL_NUMBER);
	const FQuat AuthoredRot = Authored.GetRotation();
	FQuat FrameRot;
	if (FMath::Abs(LocalAxis.Z) > 0.99f)
	{
		const FVector ZAxis = Dir * FMath::Sign(LocalAxis.Z);
		const FVector AuthoredY = AuthoredRot.GetAxisY();
		FVector YAxis = AuthoredY - ZAxis * FVector::DotProduct(AuthoredY, ZAxis);
		if (YAxis.SizeSquared() < 1.e-4f)
		{
			YAxis = FVector::CrossProduct(ZAxis, AuthoredRot.GetAxisX());
		}
		YAxis.Normalize();
		FrameRot = FRotationMatrix::MakeFromZY(ZAxis, YAxis).ToQuat();
	}
	else
	{
		// Handle axis is not weapon-Z (unusual data): shortest-arc from the authored rotation.
		FrameRot = FQuat::FindBetweenNormals(AuthoredRot.RotateVector(LocalAxis), Dir) * AuthoredRot;
	}

	FQuat NewRotation = FQuat::Slerp(AuthoredRot, FrameRot, FMath::Clamp(InOutWeight, 0.f, 1.f)).GetNormalized();

	// Rate limit (not an angle clamp): a clip cut or a degenerate hand axis must not whip the weapon more than MaxStepDeg per frame.
	const float MaxStep = FMath::DegreesToRadians(CVarGripMaxStepDegPerSec.GetValueOnGameThread()) * FMath::Max(DeltaSeconds, 0.f);
	if (bInOutHasPrevRotation && MaxStep > KINDA_SMALL_NUMBER)
	{
		const float Step = InOutPrevRotation.AngularDistance(NewRotation);
		if (Step > MaxStep)
		{
			NewRotation = FQuat::Slerp(InOutPrevRotation, NewRotation, MaxStep / Step).GetNormalized();
		}
	}
	InOutPrevRotation = NewRotation;
	bInOutHasPrevRotation = true;
	const FVector NewLocation = PrimaryWorld - NewRotation.RotateVector(Data->PrimaryGripLocal * Scale);
	Weapon->SetWorldTransform(FTransform(NewRotation, NewLocation, Scale));
}

UMeshComponent* UBH_CombatFunctionLibrary::AttachWeaponMesh(ACharacter* Character, EBH_WeaponSlot Slot, const FBH_WeaponMeshSlot& MeshSlot)
{
	using namespace BH_CombatFunctionLibrary_Private;

	if (!Character || !MeshSlot.HasMesh())
	{
		return nullptr;
	}

	// One mesh per slot: clear whatever is currently in this slot.
	if (UMeshComponent* Existing = GetEquippedWeaponComponent(Character, Slot))
	{
		Character->RemoveInstanceComponent(Existing);
		Existing->DestroyComponent();
	}

	const FName SocketName = MeshSlot.SocketOverride.IsNone() ? GetWeaponSocketForSlot(Slot) : MeshSlot.SocketOverride;
	USkeletalMeshComponent* ParentMesh = FindWeaponAttachMesh(Character, SocketName);
	if (!ParentMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("AttachWeaponMesh: '%s' has no skeletal mesh to attach to."), *Character->GetName());
		return nullptr;
	}
	if (!ParentMesh->DoesSocketExist(SocketName))
	{
		UE_LOG(LogTemp, Warning, TEXT("AttachWeaponMesh: '%s' (%s) has no socket '%s' -- attaching to the mesh root instead."),
			*Character->GetName(), *ParentMesh->GetName(), *SocketName.ToString());
	}

	UMeshComponent* NewComponent = nullptr;
	if (!MeshSlot.SkeletalMesh.IsNull())
	{
		if (USkeletalMesh* SkeletalMeshAsset = MeshSlot.SkeletalMesh.LoadSynchronous())
		{
			USkeletalMeshComponent* SkeletalComponent = NewObject<USkeletalMeshComponent>(Character, NAME_None, RF_Transient);
			SkeletalComponent->SetSkeletalMeshAsset(SkeletalMeshAsset);
			NewComponent = SkeletalComponent;
		}
	}
	else if (UStaticMesh* StaticMeshAsset = MeshSlot.StaticMesh.LoadSynchronous())
	{
		UStaticMeshComponent* StaticComponent = NewObject<UStaticMeshComponent>(Character, NAME_None, RF_Transient);
		StaticComponent->SetStaticMesh(StaticMeshAsset);
		NewComponent = StaticComponent;
	}

	if (!NewComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("AttachWeaponMesh: failed to load the mesh asset for slot %d on '%s'."), static_cast<int32>(Slot), *Character->GetName());
		return nullptr;
	}

	NewComponent->ComponentTags.Add(WeaponComponentTag());
	NewComponent->ComponentTags.Add(WeaponSlotTag(Slot));

	// Blade line for meshes without weapon_root / weapon_tip sockets (read by UANS_MeleeHitbox).
	// Also carries the two-handed grip point (read by GetSecondaryGripIKTarget).
	{
		UBH_WeaponBladeData* BladeData = NewObject<UBH_WeaponBladeData>(NewComponent);
		BladeData->bHasBladeLine = MeshSlot.bUseBladeOverride;
		BladeData->RootLocal = MeshSlot.BladeRootLocal;
		BladeData->TipLocal = MeshSlot.BladeTipLocal;
		BladeData->bTwoHandedGrip = MeshSlot.bTwoHandedGrip;
		BladeData->PrimaryGripLocal = MeshSlot.PrimaryGripLocal;
		BladeData->AuthoredRelative = MeshSlot.RelativeTransform;
		BladeData->SecondaryGripLocal = MeshSlot.SecondaryGripLocal;
		BladeData->SecondaryGripRotLocal = MeshSlot.SecondaryGripRotLocal;
		BladeData->HandSocket = SocketName;
		BladeData->HandRelative = MeshSlot.RelativeTransform;
		BladeData->SheathedSocket = MeshSlot.SheathedSocket;
		BladeData->SheathedRelative = MeshSlot.SheathedRelativeTransform;
		NewComponent->AddAssetUserData(BladeData);
	}

	// Purely visual: hit detection is done by UANS_MeleeHitbox sweeps, so the
	// mesh itself must never push the capsule or block the camera.
	NewComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	NewComponent->SetGenerateOverlapEvents(false);
	NewComponent->SetCanEverAffectNavigation(false);

	NewComponent->SetupAttachment(ParentMesh, SocketName);
	NewComponent->RegisterComponent();
	NewComponent->AttachToComponent(ParentMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, SocketName);
	NewComponent->SetRelativeTransform(MeshSlot.RelativeTransform);
	Character->AddInstanceComponent(NewComponent);

	// Two-handed main-hand weapon: make sure the post-animation aim driver exists on the character.
	if (Slot == EBH_WeaponSlot::MainHand && MeshSlot.bTwoHandedGrip && !Character->FindComponentByClass<UBH_TwoHandAimComponent>())
	{
		UBH_TwoHandAimComponent* Aim = NewObject<UBH_TwoHandAimComponent>(Character, NAME_None, RF_Transient);
		Aim->RegisterComponent();
		Character->AddInstanceComponent(Aim);
		TArray<USkeletalMeshComponent*> Meshes;
		Character->GetComponents<USkeletalMeshComponent>(Meshes);
		for (USkeletalMeshComponent* Mesh : Meshes)
		{
			Aim->AddTickPrerequisiteComponent(Mesh);
		}
	}

	return NewComponent;
}

bool UBH_CombatFunctionLibrary::SetWeaponMeshSheathed(ACharacter* Character, EBH_WeaponSlot Slot, bool bSheathed)
{
	UMeshComponent* Weapon = GetEquippedWeaponComponent(Character, Slot);
	UBH_WeaponBladeData* Data = Weapon ? Weapon->GetAssetUserData<UBH_WeaponBladeData>() : nullptr;
	if (!Weapon || !Data)
	{
		return false;
	}

	FName Socket = Data->HandSocket;
	FTransform Relative = Data->HandRelative;
	bool bOnBack = false;
	if (bSheathed && !Data->SheathedSocket.IsNone())
	{
		USkeletalMeshComponent* BackParent = FindWeaponAttachMesh(Character, Data->SheathedSocket);
		if (BackParent && BackParent->DoesSocketExist(Data->SheathedSocket))
		{
			Socket = Data->SheathedSocket;
			Relative = Data->SheathedRelative;
			bOnBack = true;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("SetWeaponMeshSheathed: '%s' has no socket '%s' -- the weapon stays in the hand."), *GetNameSafe(Character), *Data->SheathedSocket.ToString());
		}
	}

	if (USkeletalMeshComponent* Parent = FindWeaponAttachMesh(Character, Socket))
	{
		if (Weapon->GetAttachParent() != Parent || Weapon->GetAttachSocketName() != Socket)
		{
			Weapon->AttachToComponent(Parent, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Socket);
		}
		Weapon->SetRelativeTransform(Relative);
		Data->AuthoredRelative = Relative;
		Data->bSheathedOnBack = bOnBack;
		return true;
	}
	return false;
}

void UBH_CombatFunctionLibrary::UnequipWeaponMeshes(ACharacter* Character)
{
	using namespace BH_CombatFunctionLibrary_Private;

	if (!Character)
	{
		return;
	}

	TArray<UMeshComponent*> MeshComponents;
	Character->GetComponents<UMeshComponent>(MeshComponents);
	for (UMeshComponent* Component : MeshComponents)
	{
		if (Component && Component->ComponentHasTag(WeaponComponentTag()))
		{
			Character->RemoveInstanceComponent(Component);
			Component->DestroyComponent();
		}
	}
}

namespace BH_CombatFunctionLibrary_Private
{
	/** Spawns and attaches the main/off-hand meshes of one loadout entry. Shared by every equip entry point. */
	static void AttachLoadoutMeshes(ACharacter* Character, const FBH_OverlayWeaponLoadout& Loadout, TArray<UMeshComponent*>& OutAttachedComponents)
	{
		if (UMeshComponent* MainHand = UBH_CombatFunctionLibrary::AttachWeaponMesh(Character, EBH_WeaponSlot::MainHand, Loadout.MainHand))
		{
			OutAttachedComponents.Add(MainHand);
		}
		if (UMeshComponent* OffHand = UBH_CombatFunctionLibrary::AttachWeaponMesh(Character, EBH_WeaponSlot::OffHand, Loadout.OffHand))
		{
			OutAttachedComponents.Add(OffHand);
		}
	}
}

bool UBH_CombatFunctionLibrary::EquipWeaponsForStance(ACharacter* Character, const UBH_WeaponLoadoutDataAsset* Loadouts,
	FGameplayTag Stance, TArray<UMeshComponent*>& OutAttachedComponents)
{
	using namespace BH_CombatFunctionLibrary_Private;

	OutAttachedComponents.Reset();

	if (!Character)
	{
		return false;
	}

	UnequipWeaponMeshes(Character);

	if (!Loadouts)
	{
		UE_LOG(LogTemp, Warning, TEXT("EquipWeaponsForStance: no loadout data asset passed for '%s'."), *Character->GetName());
		return false;
	}

	const FBH_OverlayWeaponLoadout* Loadout = Loadouts->LoadoutsByStance.Find(Stance);
	if (!Loadout)
	{
		// Not an error: Unarmed (and any stance without an entry) means empty hands.
		return false;
	}

	AttachLoadoutMeshes(Character, *Loadout, OutAttachedComponents);
	return true;
}

// DEPRECATED shim: legacy Blueprints (CBP_BlackwoodHollow) still call this by Enum_OverlayPose display name. It maps the
// name onto the stance tag and forwards, so nothing here touches GASPALS content.
bool UBH_CombatFunctionLibrary::EquipWeaponsForOverlayPose(ACharacter* Character, const UBH_WeaponLoadoutDataAsset* Loadouts,
	const FString& OverlayPoseDisplayName, TArray<UMeshComponent*>& OutAttachedComponents)
{
	UE_LOG(LogTemp, Warning, TEXT("EquipWeaponsForOverlayPose is deprecated (pose '%s' on '%s'); forwarding to EquipWeaponsForStance."),
		*OverlayPoseDisplayName, Character ? *Character->GetName() : TEXT("None"));
	return EquipWeaponsForStance(Character, Loadouts, BH_Stance::FromLegacyName(FName(*OverlayPoseDisplayName)), OutAttachedComponents);
}

bool UBH_CombatFunctionLibrary::EquipWeaponsForCurrentOverlayPose(ACharacter* Character, const UBH_WeaponLoadoutDataAsset* Loadouts,
	TArray<UMeshComponent*>& OutAttachedComponents)
{
	const FString CurrentPose = GetCurrentOverlayPoseDisplayName(Character);
	if (CurrentPose.IsEmpty())
	{
		OutAttachedComponents.Reset();
		UE_LOG(LogTemp, Warning, TEXT("EquipWeaponsForCurrentOverlayPose: couldn't read OverlayPose on '%s'."), Character ? *Character->GetName() : TEXT("None"));
		return false;
	}
	return EquipWeaponsForOverlayPose(Character, Loadouts, CurrentPose, OutAttachedComponents);
}

// ============================================================================
// Weapon socket / grip tuning
// ============================================================================

namespace BH_CombatFunctionLibrary_Private
{
	/** Re-evaluates attachments on every live component that uses the socket's mesh (or skeleton, for skeleton sockets). */
	static void RefreshSocketUsers(const USkeletalMeshSocket* Socket, const USkeletalMesh* Mesh)
	{
		const USkeleton* SocketSkeleton = Socket ? Cast<USkeleton>(Socket->GetOuter()) : nullptr;
		for (TObjectIterator<USkeletalMeshComponent> It; It; ++It)
		{
			USkeletalMeshComponent* Component = *It;
			if (!Component || !Component->IsRegistered())
			{
				continue;
			}
			const USkeletalMesh* ComponentMesh = Component->GetSkeletalMeshAsset();
			const bool bUsesSocket = ComponentMesh == Mesh || (SocketSkeleton && ComponentMesh && ComponentMesh->GetSkeleton() == SocketSkeleton);
			if (bUsesSocket)
			{
				Component->UpdateChildTransforms();
			}
		}
	}

	/** The skeletal mesh component + socket the weapon in Slot is (or would be) attached to. */
	static USkeletalMeshComponent* ResolveWeaponSocket(ACharacter* Character, EBH_WeaponSlot Slot, FName& OutSocketName)
	{
		if (!Character)
		{
			return nullptr;
		}
		if (UMeshComponent* Weapon = UBH_CombatFunctionLibrary::GetEquippedWeaponComponent(Character, Slot))
		{
			if (USkeletalMeshComponent* Parent = Cast<USkeletalMeshComponent>(Weapon->GetAttachParent()))
			{
				OutSocketName = Weapon->GetAttachSocketName();
				return Parent;
			}
		}
		OutSocketName = UBH_CombatFunctionLibrary::GetWeaponSocketForSlot(Slot);
		return UBH_CombatFunctionLibrary::FindWeaponAttachMesh(Character, OutSocketName);
	}

	static bool ParseSlotArg(const FString& Arg, EBH_WeaponSlot& OutSlot)
	{
		if (Arg.StartsWith(TEXT("Main"), ESearchCase::IgnoreCase) || Arg.Equals(TEXT("R"), ESearchCase::IgnoreCase))
		{
			OutSlot = EBH_WeaponSlot::MainHand;
			return true;
		}
		if (Arg.StartsWith(TEXT("Off"), ESearchCase::IgnoreCase) || Arg.StartsWith(TEXT("Shield"), ESearchCase::IgnoreCase) || Arg.Equals(TEXT("L"), ESearchCase::IgnoreCase))
		{
			OutSlot = EBH_WeaponSlot::OffHand;
			return true;
		}
		return false;
	}

	/** Args: <Main|Off> dx dy dz [pitch yaw roll] */
	static bool ParseNudgeArgs(const TArray<FString>& Args, EBH_WeaponSlot& OutSlot, FVector& OutDelta, FRotator& OutRot)
	{
		if (Args.Num() < 4 || !ParseSlotArg(Args[0], OutSlot))
		{
			return false;
		}
		OutDelta = FVector(FCString::Atod(*Args[1]), FCString::Atod(*Args[2]), FCString::Atod(*Args[3]));
		OutRot = FRotator::ZeroRotator;
		if (Args.Num() >= 7)
		{
			OutRot = FRotator(FCString::Atod(*Args[4]), FCString::Atod(*Args[5]), FCString::Atod(*Args[6]));
		}
		return true;
	}

	static ACharacter* GetTuningCharacter(UWorld* World)
	{
		return World ? UGameplayStatics::GetPlayerCharacter(World, 0) : nullptr;
	}

	static void PrintTuning(ACharacter* Character)
	{
		for (const EBH_WeaponSlot Slot : { EBH_WeaponSlot::MainHand, EBH_WeaponSlot::OffHand })
		{
			const TCHAR* SlotLabel = Slot == EBH_WeaponSlot::MainHand ? TEXT("Main") : TEXT("Off");
			FName SocketName;
			USkeletalMeshComponent* Parent = ResolveWeaponSocket(Character, Slot, SocketName);
			FTransform SocketTransform;
			FName BoneName;
			if (Parent && UBH_CombatFunctionLibrary::GetMeshSocketTransform(Parent->GetSkeletalMeshAsset(), SocketName, SocketTransform, BoneName))
			{
				UE_LOG(LogTemp, Display, TEXT("[BH Tuning] %s socket %s on %s (bone %s): Loc %s Rot %s"), SlotLabel, *SocketName.ToString(),
					*GetNameSafe(Parent->GetSkeletalMeshAsset()), *BoneName.ToString(),
					*SocketTransform.GetLocation().ToString(), *SocketTransform.Rotator().ToString());
			}
			if (const UMeshComponent* Weapon = UBH_CombatFunctionLibrary::GetEquippedWeaponComponent(Character, Slot))
			{
				UE_LOG(LogTemp, Display, TEXT("[BH Tuning] %s grip offset (%s): Loc %s Rot %s Scale %s"), SlotLabel, *GetNameSafe(Weapon),
					*Weapon->GetRelativeLocation().ToString(), *Weapon->GetRelativeRotation().ToString(), *Weapon->GetRelativeScale3D().ToString());
			}
		}
	}

	static FAutoConsoleCommandWithWorldAndArgs CmdNudgeSocket(
		TEXT("BH.Weapon.NudgeSocket"),
		TEXT("BH.Weapon.NudgeSocket <Main|Off> dx dy dz [pitch yaw roll] -- nudge player 0's weapon_r_socket / shield_l_socket on its character mesh (live, not saved)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			EBH_WeaponSlot Slot;
			FVector Delta;
			FRotator Rot;
			if (!ParseNudgeArgs(Args, Slot, Delta, Rot))
			{
				UE_LOG(LogTemp, Warning, TEXT("Usage: BH.Weapon.NudgeSocket <Main|Off> dx dy dz [pitch yaw roll]"));
				return;
			}
			UBH_CombatFunctionLibrary::NudgeWeaponSocket(GetTuningCharacter(World), Slot, Delta, Rot, false);
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdNudgeGrip(
		TEXT("BH.Weapon.NudgeGrip"),
		TEXT("BH.Weapon.NudgeGrip <Main|Off> dx dy dz [pitch yaw roll] -- nudge player 0's equipped weapon offset relative to its socket (socket-local space)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			EBH_WeaponSlot Slot;
			FVector Delta;
			FRotator Rot;
			ACharacter* Character = GetTuningCharacter(World);
			if (!ParseNudgeArgs(Args, Slot, Delta, Rot))
			{
				UE_LOG(LogTemp, Warning, TEXT("Usage: BH.Weapon.NudgeGrip <Main|Off> dx dy dz [pitch yaw roll]"));
				return;
			}
			const UMeshComponent* Weapon = Character ? UBH_CombatFunctionLibrary::GetEquippedWeaponComponent(Character, Slot) : nullptr;
			if (!Weapon)
			{
				UE_LOG(LogTemp, Warning, TEXT("BH.Weapon.NudgeGrip: no weapon equipped in that slot."));
				return;
			}
			const FTransform Current = Weapon->GetRelativeTransform();
			const FTransform Updated(Current.GetRotation() * Rot.Quaternion(), Current.GetLocation() + Delta, Current.GetScale3D());
			UBH_CombatFunctionLibrary::SetEquippedWeaponOffset(Character, Slot, Updated);
			PrintTuning(Character);
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdStanceSet(
		TEXT("BH.Stance.Set"),
		TEXT("BH.Stance.Set <LegacyName> -- request a weapon stance for player 0 (Unarmed, Greatsword, SwordAndShield, DualSword)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			ACharacter* Character = GetTuningCharacter(World);
			UBH_StanceComponent* StanceComp = UBH_StanceComponent::FindStanceComponent(Character);
			if (!StanceComp || Args.Num() < 1 || !StanceComp->RequestStanceByLegacyName(FName(*Args[0])))
			{
				UE_LOG(LogTemp, Warning, TEXT("Usage: BH.Stance.Set <LegacyName> (needs a pawn with a stance component, valid and allowed name)"));
			}
		}));

	static FAutoConsoleCommandWithWorldAndArgs CmdWeaponDraw(
		TEXT("BH.Weapon.Draw"),
		TEXT("BH.Weapon.Draw [0|1] -- draw (1) or sheath (0) player 0's weapon; no argument toggles."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UBH_StanceComponent* StanceComp = UBH_StanceComponent::FindStanceComponent(GetTuningCharacter(World));
			if (!StanceComp)
			{
				return;
			}
			if (Args.Num() < 1)
			{
				StanceComp->ToggleWeaponDrawn();
			}
			else
			{
				StanceComp->RequestWeaponDrawn(FCString::Atoi(*Args[0]) != 0);
			}
		}));

#if WITH_EDITOR
	static FString StanceKeyToString(const FName& Key) { return Key.ToString(); }
	static FString StanceKeyToString(const FString& Key) { return Key; }

	/** Copies the legacy FName / FString keyed stance maps into their Stance.Weapon.* tag twins (never overwrites a tag entry). */
	template <typename TObj, typename TKey, typename TValue>
	static int32 MigrateStanceMap(TObj* Object, const TMap<TKey, TValue>& Legacy, TMap<FGameplayTag, TValue>& ByTag)
	{
		int32 Added = 0;
		for (const TPair<TKey, TValue>& Pair : Legacy)
		{
			const FGameplayTag Tag = BH_Stance::FromLegacyName(FName(*StanceKeyToString(Pair.Key)));
			if (Tag.IsValid() && !ByTag.Contains(Tag))
			{
				if (Added == 0)
				{
					Object->Modify();
				}
				ByTag.Add(Tag, Pair.Value);
				++Added;
			}
		}
		if (Added > 0)
		{
			Object->MarkPackageDirty();
		}
		return Added;
	}

	template <typename TObj>
	static bool IsMigratable(const TObj* Object)
	{
		const FString ClassName = Object->GetClass()->GetName();
		return !Object->HasAnyFlags(RF_Transient) && !ClassName.StartsWith(TEXT("SKEL_")) && !ClassName.StartsWith(TEXT("REINST_"))
			&& !Object->GetPackage()->HasAnyPackageFlags(PKG_PlayInEditor);
	}

	static void MigrateLegacyStanceMaps()
	{
		int32 Total = 0;
		const auto Report = [&Total](const UObject* Object, const TCHAR* Map, int32 Added)
		{
			Total += Added;
			UE_LOG(LogTemp, Display, TEXT("[BH Migrate] %s . %s : %d entries copied"), *GetNameSafe(Object), Map, Added);
		};
		for (TObjectIterator<UAH_GA_Block> It(RF_NoFlags); It; ++It)
		{
			if (It->HasAnyFlags(RF_ClassDefaultObject) && IsMigratable(*It))
			{
				Report(*It, TEXT("StanceGuardMontages"), MigrateStanceMap(*It, It->StanceGuardMontages, It->StanceGuardMontagesByTag));
			}
		}
		for (TObjectIterator<UAH_GA_Dodge> It(RF_NoFlags); It; ++It)
		{
			if (It->HasAnyFlags(RF_ClassDefaultObject) && IsMigratable(*It))
			{
				Report(*It, TEXT("DirectionalMontages"), MigrateStanceMap(*It, It->DirectionalMontages, It->DirectionalMontagesByTag));
				Report(*It, TEXT("StanceRootMotionScale"), MigrateStanceMap(*It, It->StanceRootMotionScale, It->StanceRootMotionScaleByTag));
			}
		}
		for (TObjectIterator<UAH_GA_HitReaction> It(RF_NoFlags); It; ++It)
		{
			if (It->HasAnyFlags(RF_ClassDefaultObject) && IsMigratable(*It))
			{
				Report(*It, TEXT("StanceHitMontages"), MigrateStanceMap(*It, It->StanceHitMontages, It->StanceHitMontagesByTag));
			}
		}
		for (TObjectIterator<UAH_GA_PostureBreak> It(RF_NoFlags); It; ++It)
		{
			if (It->HasAnyFlags(RF_ClassDefaultObject) && IsMigratable(*It))
			{
				Report(*It, TEXT("StancePostureBreakMontages"), MigrateStanceMap(*It, It->StancePostureBreakMontages, It->StancePostureBreakMontagesByTag));
			}
		}
		for (TObjectIterator<UBH_VitalsClusterWidget> It(RF_NoFlags); It; ++It)
		{
			if (It->HasAnyFlags(RF_ClassDefaultObject) && IsMigratable(*It))
			{
				Report(*It, TEXT("StanceIcons"), MigrateStanceMap(*It, It->StanceIcons, It->StanceIconsByTag));
			}
		}
		for (TObjectIterator<UBH_CombatFeelSettings> It; It; ++It)
		{
			if (!It->HasAnyFlags(RF_ClassDefaultObject) && IsMigratable(*It))
			{
				Report(*It, TEXT("TrailMaterials"), MigrateStanceMap(*It, It->TrailMaterials, It->TrailMaterialsByTag));
				Report(*It, TEXT("TrailLifetimes"), MigrateStanceMap(*It, It->TrailLifetimes, It->TrailLifetimesByTag));
			}
		}
		for (TObjectIterator<UBH_WeaponLoadoutDataAsset> It; It; ++It)
		{
			if (!It->HasAnyFlags(RF_ClassDefaultObject) && IsMigratable(*It))
			{
				Report(*It, TEXT("LoadoutsByOverlayPose"), MigrateStanceMap(*It, It->LoadoutsByOverlayPose, It->LoadoutsByStance));
			}
		}
		UE_LOG(LogTemp, Display, TEXT("[BH Migrate] done: %d entries copied. Compile and save the touched assets."), Total);
	}

	static FAutoConsoleCommand CmdMigrateStanceMaps(
		TEXT("BH.Stance.MigrateLegacyMaps"),
		TEXT("Editor: copies every loaded legacy FName/FString keyed stance map (guard / hit / posture-break / dodge montages, trail tables, loadout DA, vitals icons) into its Stance.Weapon.* tag twin."),
		FConsoleCommandDelegate::CreateStatic(&MigrateLegacyStanceMaps));
#endif

	static FAutoConsoleCommandWithWorld CmdPrintTuning(
		TEXT("BH.Weapon.PrintTuning"),
		TEXT("Logs player 0's weapon socket transforms and grip offsets."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			PrintTuning(GetTuningCharacter(World));
		}));
}

void UBH_CombatFunctionLibrary::EditorSetMontageLayout(UAnimMontage* Montage, const TArray<FName>& SectionNames, const TArray<float>& SectionTimes, const TArray<FName>& NextSections)
{
#if WITH_EDITOR
	if (!Montage || SectionNames.Num() != SectionTimes.Num() || SectionNames.Num() != NextSections.Num())
	{
		return;
	}
	Montage->Modify();
	Montage->CompositeSections.Reset();
	for (int32 Index = 0; Index < SectionNames.Num(); ++Index)
	{
		Montage->AddAnimCompositeSection(SectionNames[Index], SectionTimes[Index]);
	}
	for (FCompositeSection& Section : Montage->CompositeSections)
	{
		const int32 Found = SectionNames.IndexOfByKey(Section.SectionName);
		Section.NextSectionName = Found != INDEX_NONE ? NextSections[Found] : NAME_None;
	}
	float Longest = 0.f;
	for (const FSlotAnimationTrack& Slot : Montage->SlotAnimTracks)
	{
		Longest = FMath::Max(Longest, Slot.AnimTrack.GetLength());
	}
	Montage->SetCompositeLength(Longest);
	Montage->MarkPackageDirty();
#endif
}

bool UBH_CombatFunctionLibrary::GetMeshSocketTransform(const USkeletalMesh* Mesh, FName SocketName, FTransform& OutRelativeTransform, FName& OutBoneName)
{
	const USkeletalMeshSocket* Socket = Mesh ? Mesh->FindSocket(SocketName) : nullptr;
	if (!Socket)
	{
		OutRelativeTransform = FTransform::Identity;
		OutBoneName = NAME_None;
		return false;
	}
	OutRelativeTransform = FTransform(Socket->RelativeRotation, Socket->RelativeLocation, Socket->RelativeScale);
	OutBoneName = Socket->BoneName;
	return true;
}

bool UBH_CombatFunctionLibrary::EditorAddMeshSocket(USkeletalMesh* Mesh, FName SocketName, FName BoneName, const FTransform& RelativeTransform)
{
#if WITH_EDITOR
	if (!Mesh || SocketName.IsNone() || Mesh->GetRefSkeleton().FindBoneIndex(BoneName) == INDEX_NONE)
	{
		return false;
	}
	Mesh->Modify();
	USkeletalMeshSocket* Socket = Mesh->FindSocket(SocketName);
	if (!Socket)
	{
		Socket = NewObject<USkeletalMeshSocket>(Mesh, NAME_None, RF_Transactional);
		Socket->SocketName = SocketName;
		Mesh->AddSocket(Socket);
	}
	Socket->BoneName = BoneName;
	Socket->RelativeLocation = RelativeTransform.GetLocation();
	Socket->RelativeRotation = RelativeTransform.Rotator();
	Socket->RelativeScale = RelativeTransform.GetScale3D();
	Mesh->MarkPackageDirty();
	return true;
#else
	return false;
#endif
}

bool UBH_CombatFunctionLibrary::SetMeshSocketTransform(USkeletalMesh* Mesh, FName SocketName, const FTransform& RelativeTransform, bool bMarkAssetDirty)
{
	using namespace BH_CombatFunctionLibrary_Private;

	USkeletalMeshSocket* Socket = Mesh ? Mesh->FindSocket(SocketName) : nullptr;
	if (!Socket)
	{
		UE_LOG(LogTemp, Warning, TEXT("SetMeshSocketTransform: '%s' has no socket '%s'."), *GetNameSafe(Mesh), *SocketName.ToString());
		return false;
	}

#if WITH_EDITOR
	if (bMarkAssetDirty)
	{
		Socket->Modify();
	}
#endif

	Socket->RelativeLocation = RelativeTransform.GetLocation();
	Socket->RelativeRotation = RelativeTransform.Rotator();
	Socket->RelativeScale = RelativeTransform.GetScale3D();

	if (bMarkAssetDirty)
	{
		Socket->MarkPackageDirty();
	}

	RefreshSocketUsers(Socket, Mesh);
	return true;
}

bool UBH_CombatFunctionLibrary::NudgeWeaponSocket(ACharacter* Character, EBH_WeaponSlot Slot, FVector DeltaLocation, FRotator DeltaRotation, bool bMarkAssetDirty)
{
	using namespace BH_CombatFunctionLibrary_Private;

	FName SocketName;
	USkeletalMeshComponent* Parent = ResolveWeaponSocket(Character, Slot, SocketName);
	USkeletalMesh* Mesh = Parent ? Parent->GetSkeletalMeshAsset() : nullptr;

	FTransform Current;
	FName BoneName;
	if (!GetMeshSocketTransform(Mesh, SocketName, Current, BoneName))
	{
		UE_LOG(LogTemp, Warning, TEXT("NudgeWeaponSocket: no socket '%s' on %s's weapon mesh (%s)."),
			*SocketName.ToString(), *GetNameSafe(Character), *GetNameSafe(Mesh));
		return false;
	}

	// Location delta in the bone's space; rotation delta in the socket's own local space.
	const FTransform Updated(Current.GetRotation() * DeltaRotation.Quaternion(), Current.GetLocation() + DeltaLocation, Current.GetScale3D());
	if (!SetMeshSocketTransform(Mesh, SocketName, Updated, bMarkAssetDirty))
	{
		return false;
	}

	UE_LOG(LogTemp, Display, TEXT("[BH Tuning] %s on %s -> Loc %s Rot %s"), *SocketName.ToString(), *GetNameSafe(Mesh),
		*Updated.GetLocation().ToString(), *Updated.Rotator().ToString());
	return true;
}

bool UBH_CombatFunctionLibrary::SetEquippedWeaponOffset(ACharacter* Character, EBH_WeaponSlot Slot, const FTransform& RelativeTransform)
{
	UMeshComponent* Weapon = GetEquippedWeaponComponent(Character, Slot);
	if (!Weapon)
	{
		return false;
	}
	Weapon->SetRelativeTransform(RelativeTransform);
	if (UBH_WeaponBladeData* Data = Weapon->GetAssetUserData<UBH_WeaponBladeData>())
	{
		// Keep the sheathed / hand offsets in step so a draw / sheath toggle does not revert a live nudge.
		(Data->bSheathedOnBack ? Data->SheathedRelative : Data->HandRelative) = RelativeTransform;
		Data->AuthoredRelative = RelativeTransform;
	}
	return true;
}

bool UBH_CombatFunctionLibrary::StoreEquippedWeaponOffsetInLoadout(ACharacter* Character, EBH_WeaponSlot Slot, UBH_WeaponLoadoutDataAsset* Loadouts)
{
	const UMeshComponent* Weapon = GetEquippedWeaponComponent(Character, Slot);
	const FString PoseName = GetCurrentOverlayPoseDisplayName(Character);
	if (!Weapon || !Loadouts || PoseName.IsEmpty())
	{
		return false;
	}

	FBH_OverlayWeaponLoadout* Entry = Loadouts->FindLoadoutEntry(FName(*PoseName));
	if (!Entry)
	{
		UE_LOG(LogTemp, Warning, TEXT("StoreEquippedWeaponOffsetInLoadout: %s has no entry for overlay pose '%s'."), *Loadouts->GetName(), *PoseName);
		return false;
	}

#if WITH_EDITOR
	Loadouts->Modify();
#endif
	FBH_WeaponMeshSlot& MeshSlot = Slot == EBH_WeaponSlot::OffHand ? Entry->OffHand : Entry->MainHand;
	// While the weapon rides on its sheathed socket the nudged offset is the sheathed one.
	const UBH_WeaponBladeData* BladeData = const_cast<UMeshComponent*>(Weapon)->GetAssetUserData<UBH_WeaponBladeData>();
	if (BladeData && BladeData->bSheathedOnBack)
	{
		MeshSlot.SheathedRelativeTransform = Weapon->GetRelativeTransform();
	}
	else
	{
		MeshSlot.RelativeTransform = Weapon->GetRelativeTransform();
	}
	Loadouts->MarkPackageDirty();
	return true;
}

// ============================================================================
// GASP movement
// ============================================================================

bool UBH_CombatFunctionLibrary::SetCharacterWantsToStrafe(APawn* Pawn, bool bValue, bool* OutPrevious)
{
	if (!Pawn)
	{
		return false;
	}

	if (ABH_CharacterBase* BHCharacter = Cast<ABH_CharacterBase>(Pawn))
	{
		UE_LOG(LogBHCombat, Verbose, TEXT("SetCharacterWantsToStrafe: ABH_CharacterBase fast path (bLockOnStrafe=%d)"), bValue ? 1 : 0);
		if (OutPrevious)
		{
			*OutPrevious = BHCharacter->IsLockOnStrafeActive();
		}
		BHCharacter->SetLockOnStrafe(bValue);
		return true;
	}

	// CBP_SandboxCharacter: replicated struct "CharacterInputState" (UserDefinedStruct, members carry mangled names).
	FStructProperty* StateProp = CastField<FStructProperty>(Pawn->GetClass()->FindPropertyByName(TEXT("CharacterInputState")));
	if (!StateProp)
	{
		return false;
	}

	void* StatePtr = StateProp->ContainerPtrToValuePtr<void>(Pawn);
	FBoolProperty* StrafeProp = nullptr;
	for (TFieldIterator<FProperty> It(StateProp->Struct); It; ++It)
	{
		if (It->GetName().StartsWith(TEXT("WantsToStrafe")) || It->GetAuthoredName().Equals(TEXT("WantsToStrafe")))
		{
			StrafeProp = CastField<FBoolProperty>(*It);
			break;
		}
	}
	if (!StrafeProp)
	{
		return false;
	}

	const bool bCurrent = StrafeProp->GetPropertyValue_InContainer(StatePtr);
	if (OutPrevious)
	{
		*OutPrevious = bCurrent;
	}
	if (bCurrent == bValue)
	{
		return true; // nothing to change
	}
	StrafeProp->SetPropertyValue_InContainer(StatePtr, bValue);

	// Same path the Blueprint uses after a local input change: tell the server.
	UFunction* UpdateFn = Pawn->FindFunction(TEXT("UpdateInputState_Server"));
	if (UpdateFn && UpdateFn->ParmsSize > 0)
	{
		uint8* Params = static_cast<uint8*>(FMemory_Alloca(UpdateFn->ParmsSize));
		FMemory::Memzero(Params, UpdateFn->ParmsSize);
		for (TFieldIterator<FProperty> It(UpdateFn); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			It->InitializeValue_InContainer(Params);
		}
		FStructProperty* ParamProp = nullptr;
		for (TFieldIterator<FProperty> It(UpdateFn); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			if (FStructProperty* SP = CastField<FStructProperty>(*It))
			{
				ParamProp = SP;
				break;
			}
		}
		if (ParamProp && ParamProp->Struct == StateProp->Struct)
		{
			ParamProp->CopyCompleteValue(ParamProp->ContainerPtrToValuePtr<void>(Params), StatePtr);
			Pawn->ProcessEvent(UpdateFn, Params);
		}
		for (TFieldIterator<FProperty> It(UpdateFn); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			It->DestroyValue_InContainer(Params);
		}
	}
	return true;
}

// ============================================================================
// Animation
// ============================================================================

int32 UBH_CombatFunctionLibrary::ReplaceLinkedAnimGraphClass(USkeletalMeshComponent* Mesh, TSubclassOf<UAnimInstance> FromClass, TSubclassOf<UAnimInstance> ToClass)
{
	UAnimInstance* MainInstance = Mesh ? Mesh->GetAnimInstance() : nullptr;
	if (!MainInstance || !ToClass)
	{
		return 0;
	}

	const IAnimClassInterface* AnimClassInterface = IAnimClassInterface::GetFromClass(MainInstance->GetClass());
	if (!AnimClassInterface)
	{
		return 0;
	}

	int32 NumSwitched = 0;
	for (const FStructProperty* NodeProperty : AnimClassInterface->GetLinkedAnimGraphNodeProperties())
	{
		// Exact struct match: linked anim LAYER nodes derive from FAnimNode_LinkedAnimGraph and must be left alone.
		if (!NodeProperty || NodeProperty->Struct != FAnimNode_LinkedAnimGraph::StaticStruct())
		{
			continue;
		}

		FAnimNode_LinkedAnimGraph* LinkedNode = NodeProperty->ContainerPtrToValuePtr<FAnimNode_LinkedAnimGraph>(MainInstance);
		if (!LinkedNode)
		{
			continue;
		}

		const UAnimInstance* CurrentInstance = LinkedNode->GetTargetInstance<UAnimInstance>();
		const UClass* CurrentClass = CurrentInstance ? CurrentInstance->GetClass() : LinkedNode->InstanceClass.Get();
		if (CurrentClass == ToClass.Get())
		{
			continue;
		}
		if (FromClass && CurrentClass != FromClass.Get())
		{
			continue;
		}

		LinkedNode->SetAnimClass(ToClass, MainInstance);
		++NumSwitched;
	}

	UE_LOG(LogTemp, Log, TEXT("ReplaceLinkedAnimGraphClass: switched %d linked graph(s) on '%s' to %s."),
		NumSwitched, *GetNameSafe(Mesh->GetOwner()), *ToClass->GetName());
	return NumSwitched;
}

float UBH_CombatFunctionLibrary::GetMontageLayeringValue(const UAnimInstance* AnimInstance, FName CurveName, float StanceValue, FName CurveSlotName)
{
	if (!AnimInstance || CurveName.IsNone())
	{
		return StanceValue;
	}

	// Linked graphs / layers evaluate montages from the main instance.
	const UAnimInstance* MainInstance = AnimInstance;
	if (const USkeletalMeshComponent* OwningMesh = AnimInstance->GetOwningComponent())
	{
		if (const UAnimInstance* MeshInstance = OwningMesh->GetAnimInstance())
		{
			MainInstance = MeshInstance;
		}
	}

	// Weight-blend EVERY montage instance (including ones blending out).
	// NOTE: deliberately NOT GetActiveMontageInstance() - that drops a montage the
	// instant it starts blending out, which snapped every layer to its stance value
	// in a single frame while the montage pose was still at full weight (a visible
	// pop / upper-body twitch at the end of every attack).
	// Also NOT "heaviest instance wins": during a cross-fade between two montages
	// with different curves (attack -> hit reaction, guard -> parry, guard -> hit)
	// the winner flips when the weights cross 0.5 and the layer value jumps in one
	// frame. Summing weight * value keeps it continuous. A montage without the curve
	// contributes the stance value for its share of the weight.
	float WeightedValue = 0.f;
	float TotalWeight = 0.f;

	for (const FAnimMontageInstance* MontageInstance : MainInstance->MontageInstances)
	{
		const UAnimMontage* Montage = MontageInstance ? MontageInstance->Montage.Get() : nullptr;
		if (!Montage)
		{
			continue;
		}

		const float Weight = FMath::Clamp(MontageInstance->GetWeight(), 0.f, 1.f);
		if (Weight <= UE_KINDA_SMALL_NUMBER)
		{
			continue;
		}

		float MontageValue = StanceValue;

		// Prefer the dedicated curve track, fall back to the first slot track.
		const FAnimTrack* Track = CurveSlotName.IsNone() ? nullptr : Montage->GetAnimationData(CurveSlotName);
		if (!Track && Montage->SlotAnimTracks.Num() > 0)
		{
			Track = &Montage->SlotAnimTracks[0].AnimTrack;
		}
		if (Track)
		{
			const float MontagePosition = MontageInstance->GetPosition();
			if (const FAnimSegment* Segment = Track->GetSegmentAtTime(MontagePosition))
			{
				float PositionInAnim = 0.f;
				const UAnimSequenceBase* SegmentAnim = Segment->GetAnimationData(MontagePosition, PositionInAnim);
				if (SegmentAnim && SegmentAnim->HasCurveData(CurveName))
				{
					MontageValue = SegmentAnim->EvaluateCurveData(CurveName, FAnimExtractContext(static_cast<double>(PositionInAnim)));
				}
			}
		}

		WeightedValue += Weight * MontageValue;
		TotalWeight += Weight;
	}

	if (TotalWeight <= UE_KINDA_SMALL_NUMBER)
	{
		return StanceValue;
	}
	if (TotalWeight >= 1.f)
	{
		// Overlapping montages in different slot groups can sum past 1: normalise.
		return WeightedValue / TotalWeight;
	}
	return WeightedValue + StanceValue * (1.f - TotalWeight);
}
