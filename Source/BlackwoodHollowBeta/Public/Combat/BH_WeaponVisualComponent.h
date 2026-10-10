// Blackwood Hollow - weapon visuals on the MetaHuman visual body (Phase 12F-2, #24)
// Target: Unreal Engine 5.8 (C++)
//
// WHY: UBH_CombatFunctionLibrary::AttachWeaponMesh attaches every weapon to the hidden UEFN gameplay mesh (weapon_r_socket, shield_l_socket,
// weapon_back_r_socket, shield_back_socket). The VISIBLE character is the retargeted MetaHuman body in the VisualOverride child actor, and the
// weapon drifted 3.5 to 10 cm from its hands because the retarget only approximates the source pose.
//
// WHAT: this component keeps, per weapon slot, a purely cosmetic clone of the weapon mesh attached to the SAME-NAMED socket on the visual body
// (sockets added on the MetaHuman body skeleton, matched to the grip), and hides the source weapon component. The source component stays alive,
// attached to the gameplay mesh, so everything gameplay keeps reading it unchanged: melee hit traces (UANS_MeleeHitbox), the weapon trail, the
// two-hand aim / grip IK, the tuning commands. The clone copies the source's attach socket (hand or sheathed) and RELATIVE transform every
// frame, so draw / sheath, the two-hand aim and BH.Weapon.Nudge* all show on the visual weapon too.
//
// SWORD ARM: with the one-handed Sword (Sword & Shield with an empty off hand) the left arm must not hold a shield pose. The retarget animation
// Blueprint of the visual body (ABP_BH_MetaHumanRetarget, not the off-limits GASP ABP) owns a float variable named SwordArmAlpha that drives a
// Transform (Modify) Bone on clavicle_l; this component blends the value 0..1 from the stance (drawn + Sword variant) and writes it by reflection.
//
// If the visual body does not exist yet, or lacks the named socket, the source weapon stays visible (the old behaviour), per slot.
//
// NETWORK: cosmetic and local on every machine except dedicated servers. It is driven by the weapon components that the replicated loadout /
// stance state already creates on every machine; nothing here replicates. Clones are owned by the visual actor (like the armor pieces), so
// they are never found by GetEquippedWeaponComponent / FindWeaponAttachMesh.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Combat/BH_WeaponTypes.h"
#include "BH_WeaponVisualComponent.generated.h"

class UMeshComponent;
class USkeletalMeshComponent;

UCLASS(ClassGroup = (BlackwoodHollow), meta = (BlueprintSpawnableComponent))
class BLACKWOODHOLLOWBETA_API UBH_WeaponVisualComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBH_WeaponVisualComponent();

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|WeaponVisual")
	static UBH_WeaponVisualComponent* Find(const AActor* Actor);

	/** The visual weapon clone for Slot (null when the source weapon is shown instead). */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|WeaponVisual")
	UMeshComponent* GetVisualWeapon(EBH_WeaponSlot Slot) const;

	/** The MetaHuman visual body (null until the visual actor exists). */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|WeaponVisual")
	USkeletalMeshComponent* GetVisualBody() const { return VisualBody.Get(); }

	/**
	 * Drift sampling for tests (#24). All world positions, cm. SourceWeapon = the gameplay weapon component's origin, VisualWeapon = the clone's
	 * origin, SourceHand / VisualHand = the hand bone (hand_r for the main hand, hand_l for the off hand) on the hidden source mesh and on the
	 * visible body. Returns false when there is no clone for the slot.
	 */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|WeaponVisual")
	bool SampleDrift(EBH_WeaponSlot Slot, FVector& SourceWeapon, FVector& VisualWeapon, FVector& SourceHand, FVector& VisualHand) const;

	/** Seconds to blend the Sword left-arm relaxation in / out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|WeaponVisual", meta = (ClampMin = "0.01"))
	float SwordArmBlendTime = 0.25f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	struct FSlotState
	{
		TWeakObjectPtr<UMeshComponent> Source;
		TWeakObjectPtr<UMeshComponent> Visual;
	};

	USkeletalMeshComponent* FindVisualBody() const;
	void UpdateSlot(int32 SlotIndex, EBH_WeaponSlot Slot, USkeletalMeshComponent* Body);

	/** Writes the SwordArmAlpha variable of the visual body's anim instance (no-op when the instance has none). */
	void UpdateSwordArm(USkeletalMeshComponent* Body, float DeltaTime);

	/** Destroys the clone of one slot and shows the source weapon again. */
	void ReleaseSlot(int32 SlotIndex);

	FSlotState SlotStates[2];
	TWeakObjectPtr<USkeletalMeshComponent> VisualBody;
	float SwordArmAlpha = 0.f;
};
