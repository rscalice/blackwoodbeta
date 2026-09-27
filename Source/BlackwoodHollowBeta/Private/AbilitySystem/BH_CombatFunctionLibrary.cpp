// Blackwood Hollow - Combat setup / GASP overlay bridge function library (implementation)

#include "AbilitySystem/BH_CombatFunctionLibrary.h"
#include "AbilitySystem/AH_AttributeSet.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "Combat/BH_WeaponLoadoutDataAsset.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Abilities/GameplayAbility.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimInstance.h"

namespace BH_CombatFunctionLibrary_Private
{
	// Component tags used to find/clean up the weapon meshes this library spawns.
	// (Plain functions returning FName rather than file-scope statics, to stay
	// safe under Live Coding patches -- see the note in ApplyOverlayPoseByDisplayName.)
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

	// GASP's OverlayPose enum lives in plugin content as a UserDefinedEnum,
	// so it's loaded by soft path rather than referenced via a native UENUM.
	// (Plain local, not "static const" -- Live Coding hot-reload can leave
	// function-local statics uninitialized.)
	static UEnum* LoadOverlayPoseEnum()
	{
		const FSoftObjectPath OverlayPoseEnumPath(TEXT("/GASPALS/OverlaySystem/Blueprints/Enum_OverlayPose.Enum_OverlayPose"));
		return Cast<UEnum>(OverlayPoseEnumPath.TryLoad());
	}

	/** Reads GASP's replicated "OverlayPose" byte/enum property. Returns false if absent. */
	static bool ReadOverlayPoseValue(const AActor* TargetCharacter, int64& OutValue)
	{
		if (!TargetCharacter)
		{
			return false;
		}

		const FProperty* OverlayPoseProperty = TargetCharacter->GetClass()->FindPropertyByName(FName(TEXT("OverlayPose")));
		if (!OverlayPoseProperty)
		{
			return false;
		}

		const void* ValuePtr = OverlayPoseProperty->ContainerPtrToValuePtr<void>(TargetCharacter);
		if (const FByteProperty* ByteProperty = CastField<FByteProperty>(OverlayPoseProperty))
		{
			OutValue = ByteProperty->GetPropertyValue(ValuePtr);
			return true;
		}
		if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(OverlayPoseProperty))
		{
			OutValue = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(ValuePtr);
			return true;
		}
		return false;
	}
}

// ============================================================================
// Setup / abilities
// ============================================================================

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

	if (!ASC->GetSet<UAH_AttributeSet>())
	{
		UAH_AttributeSet* NewAttributeSet = NewObject<UAH_AttributeSet>(OwningActor, UAH_AttributeSet::StaticClass(), TEXT("AH_AttributeSet"));
		ASC->AddAttributeSetSubobject(NewAttributeSet);
	}

	if (OverloadBurstAbilityClass && OwningActor->HasAuthority())
	{
		if (!ASC->FindAbilitySpecFromClass(OverloadBurstAbilityClass))
		{
			FGameplayAbilitySpec Spec(OverloadBurstAbilityClass, 1, INDEX_NONE, OwningActor);
			ASC->GiveAbility(Spec);
		}
	}

	return true;
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

// ============================================================================
// GASP OverlayPose bridge
// ============================================================================

bool UBH_CombatFunctionLibrary::ApplyOverlayPoseByDisplayName(AActor* TargetCharacter, const FString& OverlayPoseDisplayName)
{
	using namespace BH_CombatFunctionLibrary_Private;

	if (!TargetCharacter)
	{
		return false;
	}

	UEnum* OverlayPoseEnum = LoadOverlayPoseEnum();
	if (!OverlayPoseEnum)
	{
		UE_LOG(LogTemp, Warning, TEXT("ApplyOverlayPoseByDisplayName: could not load Enum_OverlayPose from GASPALS content."));
		return false;
	}

	int32 FoundIndex = INDEX_NONE;
	const int32 NumEntries = OverlayPoseEnum->NumEnums();
	for (int32 Index = 0; Index < NumEntries; ++Index)
	{
		if (OverlayPoseEnum->GetDisplayNameTextByIndex(Index).ToString().Equals(OverlayPoseDisplayName, ESearchCase::IgnoreCase))
		{
			FoundIndex = Index;
			break;
		}
	}

	if (FoundIndex == INDEX_NONE)
	{
		UE_LOG(LogTemp, Warning, TEXT("ApplyOverlayPoseByDisplayName: no Enum_OverlayPose entry named '%s' (add it in-editor first)."), *OverlayPoseDisplayName);
		return false;
	}

	const int64 EnumValue = OverlayPoseEnum->GetValueByIndex(FoundIndex);
	const uint8 ByteValue = static_cast<uint8>(EnumValue);

	// GASP stores the active pose in a plain replicated byte property named
	// "OverlayPose" (ReplicatedUsing=OnRep_OverlayPose) on CBP_SandboxCharacter,
	// and applies it via "UpdateOverlayPose" (evaluates CHT_OverlayPoses and
	// attaches the resulting held-item overlay + prop mesh). OnRep_OverlayPose
	// itself does nothing but call UpdateOverlayPose(), so on this instance we
	// set the property directly (an authoritative/local instance never gets a
	// local OnRep call) and then call UpdateOverlayPose() ourselves to apply
	// it immediately. Remote clients still pick up the change normally via the
	// engine's own OnRep dispatch when the replicated property arrives.
	// NOTE: deliberately not "static const FName" here -- a function-local
	// static in a Live Coding hot-reloaded function can end up left as a
	// default-constructed (NAME_None) value instead of re-running its
	// initializer on some patch iterations. Plain locals sidestep that.
	const FName OverlayPosePropertyName(TEXT("OverlayPose"));
	FProperty* OverlayPoseProperty = TargetCharacter->GetClass()->FindPropertyByName(OverlayPosePropertyName);
	if (!OverlayPoseProperty)
	{
		UE_LOG(LogTemp, Warning, TEXT("ApplyOverlayPoseByDisplayName: '%s' has no '%s' property -- is it a GASP SandboxCharacter?"),
			*TargetCharacter->GetName(), *OverlayPosePropertyName.ToString());
		return false;
	}

	void* OverlayPoseValuePtr = OverlayPoseProperty->ContainerPtrToValuePtr<void>(TargetCharacter);

	// A Blueprint byte variable backed by a UserDefinedEnum can compile down
	// to either a plain FByteProperty or (as of UE5.8) an FEnumProperty
	// wrapping an underlying numeric property -- handle both rather than
	// assuming one, since guessing wrong here fails completely silently
	// (SetPropertyValue_InContainer on the wrong FProperty subtype either
	// no-ops or writes past the actual storage).
	if (FByteProperty* ByteProperty = CastField<FByteProperty>(OverlayPoseProperty))
	{
		ByteProperty->SetPropertyValue(OverlayPoseValuePtr, ByteValue);
	}
	else if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(OverlayPoseProperty))
	{
		EnumProperty->GetUnderlyingProperty()->SetIntPropertyValue(OverlayPoseValuePtr, static_cast<uint64>(ByteValue));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("ApplyOverlayPoseByDisplayName: '%s's '%s' property is a '%s', not a byte or enum property -- can't set it."),
			*TargetCharacter->GetName(), *OverlayPosePropertyName.ToString(), *OverlayPoseProperty->GetClass()->GetName());
		return false;
	}

	const FName UpdateOverlayPoseFunctionName(TEXT("UpdateOverlayPose"));
	UFunction* UpdateOverlayPoseFunction = TargetCharacter->FindFunction(UpdateOverlayPoseFunctionName);
	if (!UpdateOverlayPoseFunction)
	{
		UE_LOG(LogTemp, Warning, TEXT("ApplyOverlayPoseByDisplayName: '%s' has no '%s' function -- is it a GASP SandboxCharacter?"),
			*TargetCharacter->GetName(), *UpdateOverlayPoseFunctionName.ToString());
		return false;
	}
	TargetCharacter->ProcessEvent(UpdateOverlayPoseFunction, nullptr);

	return true;
}

FString UBH_CombatFunctionLibrary::GetCurrentOverlayPoseDisplayName(const AActor* TargetCharacter)
{
	using namespace BH_CombatFunctionLibrary_Private;

	int64 Value = 0;
	if (!ReadOverlayPoseValue(TargetCharacter, Value))
	{
		return FString();
	}

	const UEnum* OverlayPoseEnum = LoadOverlayPoseEnum();
	if (!OverlayPoseEnum)
	{
		return FString();
	}

	const int32 Index = OverlayPoseEnum->GetIndexByValue(Value);
	return Index != INDEX_NONE ? OverlayPoseEnum->GetDisplayNameTextByIndex(Index).ToString() : FString();
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

	return NewComponent;
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

bool UBH_CombatFunctionLibrary::EquipWeaponsForOverlayPose(ACharacter* Character, const UBH_WeaponLoadoutDataAsset* Loadouts,
	const FString& OverlayPoseDisplayName, TArray<UMeshComponent*>& OutAttachedComponents)
{
	OutAttachedComponents.Reset();

	if (!Character)
	{
		return false;
	}

	UnequipWeaponMeshes(Character);

	if (!Loadouts)
	{
		UE_LOG(LogTemp, Warning, TEXT("EquipWeaponsForOverlayPose: no loadout data asset passed for '%s'."), *Character->GetName());
		return false;
	}

	FBH_OverlayWeaponLoadout Loadout;
	if (!Loadouts->FindLoadout(FName(*OverlayPoseDisplayName), Loadout))
	{
		// Not an error: e.g. "Default" overlay = empty hands.
		return false;
	}

	if (UMeshComponent* MainHand = AttachWeaponMesh(Character, EBH_WeaponSlot::MainHand, Loadout.MainHand))
	{
		OutAttachedComponents.Add(MainHand);
	}
	if (UMeshComponent* OffHand = AttachWeaponMesh(Character, EBH_WeaponSlot::OffHand, Loadout.OffHand))
	{
		OutAttachedComponents.Add(OffHand);
	}
	return true;
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
