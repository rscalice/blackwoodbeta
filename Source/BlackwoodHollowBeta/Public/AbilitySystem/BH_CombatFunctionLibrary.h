// Blackwood Hollow - Combat setup / GASP overlay bridge function library
// Target: Unreal Engine 5.8 (C++), GAS (GameplayAbilities plugin required)
//
// Bridges our GAS combat layer (UAH_AttributeSet, UBPC_HeartFragment) onto
// the character: one-time ASC/AttributeSet/ability-grant setup a Blueprint
// BeginPlay can call in a single node, weapon mesh equip, and stance helpers.
// Phase 7: the GASP OverlayPose reflection bridge (and its /GASPALS enum soft path)
// is gone; the *OverlayPose* functions below are deprecated shims over UBH_StanceComponent.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "Combat/BH_WeaponTypes.h"
#include "Combat/BH_CombatTeam.h"
#include "BH_CombatFunctionLibrary.generated.h"

class UGameplayAbility;
class UAbilitySystemComponent;
class ACharacter;
class UMeshComponent;
class USkeletalMeshComponent;
class UAnimInstance;
class UAnimMontage;
class UBH_WeaponLoadoutDataAsset;
class USkeletalMesh;
class APawn;
class UUserWidget;
class UBH_HUDWidget;
class UTexture2D;

UCLASS()
class BLACKWOODHOLLOWBETA_API UBH_CombatFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * One-shot combat setup for a character: resolves its AbilitySystemComponent
	 * (via IAbilitySystemInterface, falling back to a component search), calls
	 * InitAbilityActorInfo, spawns + registers UAH_AttributeSet if not already
	 * present, and (server-only) hands off to the character's UBPC_HeartFragment to grant its fragment loadout.
	 * OverloadBurstAbilityClass is DEPRECATED and ignored: Overload Burst is now a fragment in
	 * UBPC_HeartFragment::EquippedFragments (granting it here too would double-grant).
	 * Call once from the character's BeginPlay.
	 * @return true if an AbilitySystemComponent was found and initialized.
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Combat")
	static bool SetupCombatCharacter(AActor* OwningActor, TSubclassOf<UGameplayAbility> OverloadBurstAbilityClass);

	/**
	 * Heart-Fragment key N: fires slot Slot (0-based) of OwningActor's UBPC_HeartFragment. Wire to IA_Fragment_N Started.
	 * @return true if an activation was started.
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Combat")
	static bool HandleFragmentInput(AActor* OwningActor, int32 Slot);

	/**
	 * Data-driven stance emblem: StanceIcon of the loadout entry for PoseDisplayName in Character's weapon loadouts
	 * (its "WeaponLoadouts" object variable, else its UBH_StanceWatcherComponent::FallbackLoadouts). Null if none.
	 */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Overlay")
	static UTexture2D* GetStanceIconForPose(const AActor* Character, FName PoseDisplayName);

	/**
	 * Data-driven melee combo ability for a stance: MeleeAbility of the loadout entry for PoseDisplayName
	 * (same lookup as GetStanceIconForPose). Null if the entry is missing or has no ability set.
	 */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Overlay")
	static TSubclassOf<UGameplayAbility> GetMeleeAbilityForPose(const AActor* Character, FName PoseDisplayName);

	/**
	 * DEPRECATED shim kept so legacy Blueprints still compile. Maps the legacy stance display name to its tag and calls
	 * UBH_StanceComponent::SetStance (authority only). A character without a stance component logs a warning and
	 * returns false: the GASP OverlayPose property write was removed with the GASPALS plugin dependency.
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Overlay", meta = (DeprecatedFunction, DeprecationMessage = "Use UBH_StanceComponent::SetStance / RequestStance."))
	static bool ApplyOverlayPoseByDisplayName(AActor* TargetCharacter, const FString& OverlayPoseDisplayName);

	/**
	 * Index of the stance that follows the character's current overlay pose in StanceCycle
	 * (wrapping). Returns 0 if the current pose isn't in the list, INDEX_NONE if the list is empty.
	 */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Overlay")
	static int32 GetNextStanceIndex(const AActor* TargetCharacter, const TArray<FString>& StanceCycle);

	/**
	 * Multiplayer-correct stance change: applies the pose directly where TargetCharacter has
	 * authority, otherwise asks the server (via the character's UBH_StanceWatcherComponent).
	 * Weapon meshes follow on every machine through the watcher. Safe to call from a local controller.
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Overlay")
	static bool RequestStanceByName(AActor* TargetCharacter, const FString& OverlayPoseDisplayName);

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

	/**
	 * Hold-to-block input. bPressed = true activates BlockAbilityClass (if not
	 * already active); false cancels it, which lowers the guard.
	 * Wire to the block action's Started (true) and Completed (false).
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Combat")
	static bool HandleBlockInput(AActor* OwningActor, TSubclassOf<UGameplayAbility> BlockAbilityClass, bool bPressed);

	/**
	 * Dodge button: tries to activate whichever granted ability carries the tag Ability.Combat.Dodge
	 * (UAH_GA_Dodge picks the direction montage itself). Wire to the dodge action's Started.
	 * @return true if the ability activated.
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Combat")
	static bool HandleDodgeInput(AActor* OwningActor);

	/**
	 * Parry button: tries to activate whichever granted ability carries the tag Ability.Combat.Parry.
	 * @return true if the ability activated.
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Combat")
	static bool HandleParryInput(AActor* OwningActor);

	// -- Stamina ---------------------------------------------------------------

	/**
	 * Can ASC's owner afford Cost? Requires Stamina >= Cost, or just Stamina > 0 when bAllowOvercommit
	 * (the souls feel for attacks: you may start a swing on a sliver of stamina, it just floors at 0).
	 * Cost <= 0 is always affordable.
	 */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Combat|Stamina")
	static bool CheckStaminaCost(const UAbilitySystemComponent* ASC, float Cost, bool bAllowOvercommit = false);

	/**
	 * Spends Cost stamina (instant UAH_GE_StaminaCost, SetByCaller Data.StaminaCost). Needs authority or a valid
	 * prediction key (i.e. call it from an ability's commit / activation). Stamina floors at 0; any spend starts the
	 * bh.Combat.StaminaRegenDelay pause. Cost <= 0 does nothing.
	 * @return true if the effect was applied.
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Combat|Stamina")
	static bool ApplyStaminaCost(UAbilitySystemComponent* ASC, float Cost);

	/**
	 * Applies the passive regeneration effects (UAH_GE_PostureRegen, UAH_GE_StaminaRegen)
	 * to OwningActor's ASC once. Authority only; safe to call repeatedly (already-active
	 * effects are skipped). SetupCombatCharacter and ABH_EnemyBase call this for you.
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Combat")
	static void ApplyPassiveRegenEffects(AActor* OwningActor);

	// -- HUD -----------------------------------------------------------------

	/**
	 * Creates (once) and binds the local player's HUDs for Pawn: MainHUDClass (e.g. WBP_HUD_Main)
	 * is shown and initialised with Pawn's ASC; DebugHUDClass (e.g. W_BH_DebugHUD) is created
	 * hidden. Call from the character's BeginPlay on every machine: it only acts where Pawn is
	 * locally controlled, and retries for a few seconds if possession hasn't arrived yet.
	 * Toggle with the BH.HUD.ToggleDebug console command or ToggleDebugHUD.
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|HUD")
	static void SetupPlayerHUD(APawn* Pawn, TSubclassOf<UBH_HUDWidget> MainHUDClass, TSubclassOf<UUserWidget> DebugHUDClass);

	/** Switches the local player(s) between the production HUD and the debug HUD. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|HUD", meta = (WorldContext = "WorldContextObject"))
	static void ToggleDebugHUD(const UObject* WorldContextObject);

	// -- Teams / friendly fire -------------------------------------------------

	/**
	 * Combat team of Actor: its own IGenericTeamAgentInterface team (ABH_EnemyBase), else its
	 * controller's, else Players for a human-controlled pawn, else Neutral. Non-pawn actors
	 * (projectiles, traps) resolve through their Instigator pawn.
	 */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Combat|Teams")
	static EBH_CombatTeam GetCombatTeam(const AActor* Actor);

	/** True if both actors resolve to the same (non-Neutral) team -- i.e. they must not damage each other. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Combat|Teams")
	static bool AreCombatAllies(const AActor* A, const AActor* B);

	/** C++ form of GetCombatTeam returning the raw engine team id (NoTeam = 255). */
	static FGenericTeamId GetCombatTeamId(const AActor* Actor);

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

	/**
	 * Re-attaches the weapon in Slot (no respawn): bSheathed = its SheathedSocket + SheathedRelativeTransform (when the slot has
	 * a sheathed socket and the mesh has it), else the hand socket + the authored grip. Cosmetic / local.
	 * @return true if a weapon was found.
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Weapons")
	static bool SetWeaponMeshSheathed(ACharacter* Character, EBH_WeaponSlot Slot, bool bSheathed);

	/** Destroys every weapon mesh component previously spawned by this library on Character. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Weapons")
	static void UnequipWeaponMeshes(ACharacter* Character);

	/** The weapon mesh currently attached in Slot (nullptr if empty). Used by UANS_MeleeHitbox. */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Weapons")
	static UMeshComponent* GetEquippedWeaponComponent(const AActor* Character, EBH_WeaponSlot Slot);

	/**
	 * Two-handed weapon left-hand IK target (FBH_WeaponMeshSlot::bTwoHandedGrip). GAME THREAD ONLY: call it from the anim
	 * instance's game-thread update (e.g. ABP_BH_LayerBlending::UpdateAttackLayering) and cache the results in variables
	 * the anim graph reads.
	 *  - OutCS_Target / OutCS_JointTarget: grip point and elbow hint in AnimMesh (CharacterMesh0) COMPONENT space.
	 *  - OutHandR_Offset: the same grip point expressed in the hand_r BONE's local space. Feeding this to the Two Bone IK
	 *    (effector space = Bone Space, bone = hand_r) keeps the effector glued to the right hand's CURRENT-frame pose, so
	 *    it has no one-frame lag (the weapon / grip transform read here is last frame's).
	 *  - OutAlpha: 1 when the equipped main-hand weapon has a two-handed grip, the character is grounded, not ragdolling,
	 *    and the grip is within the left arm's reach; fades to 0 as the grip moves out of reach (e.g. extended lunges),
	 *    else 0 (outputs are then zero).
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Weapons")
	static void GetSecondaryGripIKTarget(ACharacter* Character, USkeletalMeshComponent* AnimMesh, FVector& OutCS_Target, FVector& OutCS_JointTarget, FVector& OutHandR_Offset, float& OutAlpha);

	/**
	 * Clears current weapon meshes, then attaches the loadout registered in Loadouts->LoadoutsByStance for Stance
	 * (Stance.Weapon.*). A stance with no entry (Unarmed) just leaves the hands empty.
	 * @return true if a loadout entry was found (even if it had no meshes).
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Weapons")
	static bool EquipWeaponsForStance(ACharacter* Character, const UBH_WeaponLoadoutDataAsset* Loadouts,
		FGameplayTag Stance, TArray<UMeshComponent*>& OutAttachedComponents);

	/** DEPRECATED shim: maps the legacy display name (e.g. "SwordAndShield") to a stance tag and forwards to EquipWeaponsForStance. Logs a warning. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Weapons", meta = (DeprecatedFunction, DeprecationMessage = "Use EquipWeaponsForStance."))
	static bool EquipWeaponsForOverlayPose(ACharacter* Character, const UBH_WeaponLoadoutDataAsset* Loadouts,
		const FString& OverlayPoseDisplayName, TArray<UMeshComponent*>& OutAttachedComponents);

	/** DEPRECATED: same as EquipWeaponsForOverlayPose, using the character's current stance legacy name. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Weapons", meta = (DeprecatedFunction, DeprecationMessage = "Use EquipWeaponsForStance."))
	static bool EquipWeaponsForCurrentOverlayPose(ACharacter* Character, const UBH_WeaponLoadoutDataAsset* Loadouts,
		TArray<UMeshComponent*>& OutAttachedComponents);

	/** Legacy display name of the character's current stance (empty if it has no UBH_StanceComponent). */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Overlay")
	static FString GetCurrentOverlayPoseDisplayName(const AActor* TargetCharacter);

	// -- Weapon socket / grip tuning --------------------------------------------
	// Two layers, pick the one that matches what's wrong:
	//  * Socket  (weapon_r_socket / shield_l_socket on SKM_Manny / SKM_UEFN_Mannequin):
	//    where the HAND holds things. Per character mesh, shared by every weapon.
	//    NOTE: those meshes live in plugin/third-party content -- changes apply live but are
	//    only saved to disk if bMarkAssetDirty is true and you then save the mesh.
	//  * Grip offset (FBH_WeaponMeshSlot::RelativeTransform in our DA_WeaponLoadouts):
	//    how one particular WEAPON sits in that hand. Saved in our own content.
	// Console (PIE, player 0): BH.Weapon.NudgeSocket / BH.Weapon.NudgeGrip / BH.Weapon.PrintTuning.

	/**
	 * Editor only (content authoring from Python): replaces the montage's composite sections (parallel arrays: name, start time,
	 * next section; NAME_None = end) and recalculates its play length from the slot tracks. Python cannot write either.
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Editor", meta = (DevelopmentOnly))
	static void EditorSetMontageLayout(UAnimMontage* Montage, const TArray<FName>& SectionNames, const TArray<float>& SectionTimes, const TArray<FName>& NextSections);

	/**
	 * Editor only (content authoring from Python, where socket name / bone are read-only): adds a mesh socket, or updates it
	 * when a socket of that name exists. Marks the mesh package dirty; save the mesh afterwards.
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Editor", meta = (DevelopmentOnly))
	static bool EditorAddMeshSocket(USkeletalMesh* Mesh, FName SocketName, FName BoneName, const FTransform& RelativeTransform);

	/** Reads a socket's bone and transform relative to that bone (mesh sockets first, then skeleton sockets). */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Weapons|Tuning")
	static bool GetMeshSocketTransform(const USkeletalMesh* Mesh, FName SocketName, FTransform& OutRelativeTransform, FName& OutBoneName);

	/**
	 * Overwrites a socket's transform relative to its bone. Takes effect immediately on every
	 * component using Mesh (in PIE too). bMarkAssetDirty marks the owning package for saving.
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Weapons|Tuning")
	static bool SetMeshSocketTransform(USkeletalMesh* Mesh, FName SocketName, const FTransform& RelativeTransform, bool bMarkAssetDirty = false);

	/**
	 * Nudges Slot's socket (weapon_r_socket / shield_l_socket, or the SocketOverride the weapon
	 * was attached with) on the mesh Character's weapons attach to. Rotation is applied in the
	 * socket's local space. Works on Manny or UEFN -- whichever mesh the character uses.
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Weapons|Tuning")
	static bool NudgeWeaponSocket(ACharacter* Character, EBH_WeaponSlot Slot, FVector DeltaLocation, FRotator DeltaRotation, bool bMarkAssetDirty = false);

	/** Sets the equipped weapon component's grip offset (relative to its socket) live. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Weapons|Tuning")
	static bool SetEquippedWeaponOffset(ACharacter* Character, EBH_WeaponSlot Slot, const FTransform& RelativeTransform);

	/**
	 * Copies the equipped weapon's current grip offset into Loadouts' entry for the character's
	 * current overlay pose (FBH_WeaponMeshSlot::RelativeTransform) and marks the data asset dirty.
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Weapons|Tuning")
	static bool StoreEquippedWeaponOffsetInLoadout(ACharacter* Character, EBH_WeaponSlot Slot, UBH_WeaponLoadoutDataAsset* Loadouts);

	// -- GASP movement ---------------------------------------------------------

	/**
	 * Sets the GASP CharacterInputState.WantsToStrafe flag (reflection: the struct is a UserDefinedStruct with
	 * mangled member names) and forwards it to the server through UpdateInputState_Server like the Blueprint does.
	 * While set, the character turns toward the controller rotation (lock-on camera, AI SetFocus).
	 * @param OutPrevious receives the value before the call, if non-null.
	 * @return false if the pawn is not a GASP SandboxCharacter (no such struct / member).
	 */
	static bool SetCharacterWantsToStrafe(APawn* Pawn, bool bValue, bool* OutPrevious = nullptr);

	// -- Animation -------------------------------------------------------------

	/**
	 * Swaps the class used by Linked Anim Graph nodes in Mesh's anim instance at
	 * runtime, so a cloned sub-graph (e.g. our ABP_BH_LayerBlending) can replace
	 * a plugin one (GASP's ABP_LayerBlending) without editing the plugin's ABP.
	 * Only nodes currently running FromClass are changed (any node if FromClass
	 * is None). Linked anim LAYERS are not touched.
	 * Call after the mesh's anim instance exists (BeginPlay), and again if the
	 * mesh's anim class is re-initialised.
	 * @return number of linked graph nodes switched.
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Animation")
	static int32 ReplaceLinkedAnimGraphClass(USkeletalMeshComponent* Mesh, TSubclassOf<UAnimInstance> FromClass, TSubclassOf<UAnimInstance> ToClass);

	/**
	 * Layering value for a GASP region while a montage plays: evaluates CurveName on
	 * the animation under the active montage's CurveSlotName track (falls back to
	 * the first track) and blends StanceValue toward it by the montage's blend
	 * weight. Returns StanceValue when no montage plays or the anim lacks the curve.
	 * Works from linked instances (uses the owning mesh's main anim instance).
	 * Call on the game thread (e.g. Event Blueprint Update Animation).
	 */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Animation", meta = (DefaultToSelf = "AnimInstance"))
	static float GetMontageLayeringValue(const UAnimInstance* AnimInstance, FName CurveName, float StanceValue, FName CurveSlotName = "Curves");

	/**
	 * Two-hand weapon grip frame (game thread, call after animation, e.g. from UBH_TwoHandAimComponent in TG_PostUpdateWork).
	 * While the MainHand weapon has a two-handed grip: poses the weapon from the VISIBLE mesh's hands. The right-hand grip
	 * point (PrimaryGripLocal under the authored attach) stays pinned; the handle axis (primary -> secondary grip) points at the
	 * left palm (hand_l toward middle_01_l by bh.GripIK.PalmFrac); the roll is the authored weapon Y made orthogonal to that axis.
	 * No angle clamp. When the hands are closer than bh.GripIK.MinHandGap or farther than bh.GripIK.MaxHandGap (one-handed
	 * moments), or the BH_HandIK_L montage curve is 0, it blends back to the authored attach (bh.GripIK.FrameBlendRate).
	 * InOutWeight / InOutPrevRotation / bInOutHasPrevRotation are caller-owned state (smoothed weight; last driven rotation for the
	 * rate limit bh.GripIK.MaxStepDegPerSec, frame-rate independent). bh.GripIK.FrameWeight scales the weight.
	 */
	static void ApplyTwoHandAim(ACharacter* Character, float DeltaSeconds, float& InOutWeight, FQuat& InOutPrevRotation, bool& bInOutHasPrevRotation);
};
