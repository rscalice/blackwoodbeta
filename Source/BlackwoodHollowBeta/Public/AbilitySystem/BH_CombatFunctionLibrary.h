// Blackwood Hollow - Combat setup / GASP overlay bridge function library
// Target: Unreal Engine 5.8 (C++), GAS (GameplayAbilities plugin required)
//
// Bridges our GAS combat layer (UAH_AttributeSet, UBPC_HeartFragment) onto
// the character, which is a Blueprint-only chain rooted in the
// "GASPALS" plugin (Epic's Game Animation Sample, "GASP", packaged by Polygon
// Hive). GASP has no C++ base we can reparent onto and no BlueprintCallable
// API for switching its OverlayPose from outside code, so this library:
//   1) does the one-time ASC/AttributeSet/ability-grant setup a Blueprint
//      BeginPlay can call in a single node, and
//   2) drives GASP's existing OverlayPose replication by writing directly
//      into its replicated "OverlayPose" byte property and then calling its
//      "UpdateOverlayPose" function (the same function its own
//      OnRep_OverlayPose forwards to) via reflection -- looked up by the
//      *display name* of the target Enum_OverlayPose entry, so it keeps
//      working if GASP's internal enum ordinals ever change.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "Combat/BH_WeaponTypes.h"
#include "BH_CombatFunctionLibrary.generated.h"

class UGameplayAbility;
class ACharacter;
class UMeshComponent;
class USkeletalMeshComponent;
class UBH_WeaponLoadoutDataAsset;

UCLASS()
class BLACKWOODHOLLOWBETA_API UBH_CombatFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * One-shot combat setup for a character: resolves its AbilitySystemComponent
	 * (via IAbilitySystemInterface, falling back to a component search), calls
	 * InitAbilityActorInfo, spawns + registers UAH_AttributeSet if not already
	 * present, and (server-only) grants OverloadBurstAbilityClass if set.
	 * Call once from the character's BeginPlay.
	 * @return true if an AbilitySystemComponent was found and initialized.
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Combat")
	static bool SetupCombatCharacter(AActor* OwningActor, TSubclassOf<UGameplayAbility> OverloadBurstAbilityClass);

	/**
	 * Sets the GASP OverlayPose on TargetCharacter by looking up the entry in
	 * /GASPALS/OverlaySystem/Blueprints/Enum_OverlayPose.Enum_OverlayPose whose
	 * *display name* matches OverlayPoseDisplayName (case-insensitive), writes
	 * that entry's byte value directly into CBP_SandboxCharacter's replicated
	 * "OverlayPose" property, then calls its "UpdateOverlayPose" function
	 * (the same function GASP's own OnRep_OverlayPose forwards to) so the
	 * change is applied immediately on this instance. Remote clients still
	 * pick up the change normally via the engine's own OnRep dispatch when
	 * the replicated property arrives over the network.
	 * @return false if the enum, the matching entry, the property, or the
	 *         function isn't found.
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Overlay")
	static bool ApplyOverlayPoseByDisplayName(AActor* TargetCharacter, const FString& OverlayPoseDisplayName);

	/**
	 * Grants each ability class (once -- already-granted classes are skipped).
	 * Server/authority only; no-op on clients. Call after SetupCombatCharacter.
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Combat")
	static void GrantCombatAbilities(AActor* OwningActor, const TArray<TSubclassOf<UGameplayAbility>>& AbilityClasses);

	/**
	 * Single entry point for the attack button. If an instance of MeleeAbilityClass
	 * is already running, sends Event.Combat.Input.Attack so the ability can buffer
	 * the press as a combo input; otherwise tries to activate the ability.
	 * @return true if the input was consumed (event sent or ability activated).
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Combat")
	static bool HandleMeleeAttackInput(AActor* OwningActor, TSubclassOf<UGameplayAbility> MeleeAbilityClass);

	// -- Weapon mesh attachment ------------------------------------------------

	/** Default socket for a slot: MainHand -> 'weapon_r_socket', OffHand -> 'shield_l_socket'. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Weapons")
	static FName GetWeaponSocketForSlot(EBH_WeaponSlot Slot);

	/**
	 * The skeletal mesh weapons should attach to: the first VISIBLE skeletal mesh
	 * on the character that has SocketName (on a GASP retarget setup that's the
	 * visible "Manny" mesh, not the hidden CharacterMesh0), falling back to any
	 * mesh with the socket, then to Character->GetMesh().
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Weapons")
	static USkeletalMeshComponent* FindWeaponAttachMesh(ACharacter* Character, FName SocketName);

	/**
	 * Spawns one Static/Skeletal mesh component from MeshSlot and attaches it to
	 * the slot's socket (or MeshSlot.SocketOverride). Replaces whatever was in
	 * that slot. Cosmetic/local: call on every machine (e.g. from OnRep of the
	 * overlay pose), it is not replicated.
	 * @return the new component, or nullptr if MeshSlot has no mesh / no socket found.
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Weapons")
	static UMeshComponent* AttachWeaponMesh(ACharacter* Character, EBH_WeaponSlot Slot, const FBH_WeaponMeshSlot& MeshSlot);

	/** Destroys every weapon mesh component previously spawned by this library on Character. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Weapons")
	static void UnequipWeaponMeshes(ACharacter* Character);

	/** The weapon mesh currently attached in Slot (nullptr if empty). Used by UANS_MeleeHitbox. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Weapons")
	static UMeshComponent* GetEquippedWeaponComponent(const AActor* Character, EBH_WeaponSlot Slot);

	/**
	 * Clears current weapon meshes, then attaches the loadout registered in
	 * Loadouts for OverlayPoseDisplayName (an Enum_OverlayPose display name, e.g.
	 * "SwordAndShield"). A pose with no entry just leaves the hands empty.
	 * @return true if a loadout entry was found (even if it had no meshes).
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Weapons")
	static bool EquipWeaponsForOverlayPose(ACharacter* Character, const UBH_WeaponLoadoutDataAsset* Loadouts,
		const FString& OverlayPoseDisplayName, TArray<UMeshComponent*>& OutAttachedComponents);

	/** Same as EquipWeaponsForOverlayPose, reading the character's current GASP OverlayPose. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Weapons")
	static bool EquipWeaponsForCurrentOverlayPose(ACharacter* Character, const UBH_WeaponLoadoutDataAsset* Loadouts,
		TArray<UMeshComponent*>& OutAttachedComponents);

	/** Display name of the character's current GASP OverlayPose (empty if it can't be read). */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Overlay")
	static FString GetCurrentOverlayPoseDisplayName(const AActor* TargetCharacter);
};
