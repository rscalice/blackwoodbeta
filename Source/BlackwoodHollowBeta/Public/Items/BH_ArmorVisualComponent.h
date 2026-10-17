// Blackwood Hollow - Phase 11E armor visuals on the MetaHuman visual body
// Target: Unreal Engine 5.8 (C++), Narrative Inventory/Equipment
//
// Lives on ABH_CharacterBase (player pawns) on EVERY machine except dedicated servers. Purely cosmetic: it reads the replicated
// equipment state (the active UBH_ArmorItem instances in the PlayerState inventory, found through UBH_LoadoutComponent) and keeps ONE
// primitive component per armor slot (Head, Chest, Arms, Legs, Feet) on the visual body actor:
//   * skeletal pieces: USkeletalMeshComponent, Leader-Posed to the visual BODY component (found as the child actor of the
//     ChildActorComponent tagged "VisualOverride", the component named "Body") -- never to the hidden UEFN gameplay mesh;
//   * static pieces (the Villager leather cap): UStaticMeshComponent attached to a bone of the visual body.
// A slot with no armor (or an armor item whose Visual has no mesh) shows the starting outfit piece of UBH_RPGSettings::StartingOutfit.
//
// It polls a cheap signature every RefreshInterval seconds instead of relying on events, so it copes with the VisualOverride child actor
// spawning after BeginPlay, being re-created, and the PlayerState / inventory arriving late on clients. Changes rebuild only the affected slot.
//
// Rules applied each refresh:
//   * pieces copy the body's custom depth settings (lock-on outline stays consistent), cast shadows, have no collision;
//   * HiddenMaterialSlots of a piece are hidden on its own mesh (combined meshes); HiddenBodyMaterialSlots are hidden on the body;
//   * while any visible piece has bHidesHair (helm / cap), groom components whose name contains one of HairNameTokens are hidden.
//
// Debug: bh.Armor.Give <Light|Medium|Heavy|Clear> (BH_ArmorDebugCommands.cpp) calls ServerDebugArmorSet, which grants and equips a whole set
// from UBH_RPGSettings::DebugArmorSets on the server.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TimerManager.h"
#include "Items/BH_EquipmentTypes.h"
#include "BH_ArmorVisualComponent.generated.h"

class UPrimitiveComponent;
class USkeletalMeshComponent;
class UMaterialInstanceDynamic;
class UTexture2D;
struct FBH_ArmorVisual;

UCLASS(ClassGroup = (BlackwoodHollow), meta = (BlueprintSpawnableComponent))
class BLACKWOODHOLLOWBETA_API UBH_ArmorVisualComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBH_ArmorVisualComponent();

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|ArmorVisual")
	static UBH_ArmorVisualComponent* Find(const AActor* Actor);

	/** Component tag of the ChildActorComponent that spawns the visual body actor (GASP visual override). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|ArmorVisual")
	FName VisualOverrideTag = TEXT("VisualOverride");

	/** Name of the visual actor's body skeletal mesh component (the one running the retarget ABP). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|ArmorVisual")
	FName BodyComponentName = TEXT("Body");

	/** Seconds between refresh passes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|ArmorVisual", meta = (ClampMin = "0.05"))
	float RefreshInterval = 0.2f;

	/** Groom components on the visual actor whose name contains one of these (case-insensitive) are hidden while a helm is worn. Eyebrows / eyelashes / beard are not matched by the default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|ArmorVisual")
	TArray<FString> HairNameTokens;

	/** Re-evaluates everything now (normally runs on the timer). */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|ArmorVisual")
	void RefreshNow();

	/** The visual body currently used as the leader pose component (null until the visual actor exists). */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|ArmorVisual")
	USkeletalMeshComponent* GetVisualBody() const { return VisualBody.Get(); }

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|ArmorVisual")
	bool IsHairHidden() const { return bHairHidden; }

	/** The primitive currently drawing Slot (null for none). */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|ArmorVisual")
	UPrimitiveComponent* GetSlotComponent(EBH_EquipSlot Slot) const;

	/** bh.Armor.Give: server grants (once) and equips every piece of the named debug set, unequipping other armor. "Clear" unequips all armor. Compiled to a no-op in Shipping. */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerDebugArmorSet(const FString& SetName);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** What one slot currently shows. */
	struct FSlotState
	{
		/** The UBH_ArmorItem or (for the starting outfit) the settings object the visual was built from. */
		TWeakObjectPtr<const UObject> Source;
		TWeakObjectPtr<UPrimitiveComponent> Component;
	};

	static constexpr int32 NumVisualSlots = 5; // Head, Chest, Arms, Legs, Feet = the first five EBH_EquipSlot values

	void Refresh();
	USkeletalMeshComponent* FindVisualBody() const;
	void BuildSlot(int32 SlotIndex, USkeletalMeshComponent* Body, const FBH_ArmorVisual& Visual, const UObject* Source);
	void ClearSlot(int32 SlotIndex);
	void ResetAllSlots();
	void ApplyHairHidden(AActor* VisualActor, bool bHide);
	void ApplyBodyHiddenSlots(USkeletalMeshComponent* Body, const TSet<int32>& Desired);
	void SyncCustomDepth(const USkeletalMeshComponent* Body) const;

	/** Writes the per-slot BH_Mask_* textures to the body's dynamic material instances (black when a slot has no mask or bh.Armor.BodyMask is 0). */
	void ApplyBodyMasks(USkeletalMeshComponent* Body, const UTexture2D* const (&DesiredMasks)[NumVisualSlots], bool bForce);

	/** Lazily created 1x1 black texture used when a slot has no mask. */
	UTexture2D* GetBlackMask();

	/** Body material slots that get a dynamic instance; empty = every slot of the body. */
	UPROPERTY(EditAnywhere, Category = "BlackwoodHollow|ArmorVisual")
	TArray<int32> BodyMaskMaterialSlots;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> BlackMask;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> BodyMIDs;

	/** What was last written per slot (nullptr = black) and whether masking was on, to skip redundant writes. */
	TWeakObjectPtr<const UTexture2D> AppliedMasks[NumVisualSlots];
	bool bAppliedMaskingOn = true;
	bool bMasksApplied = false;

	FSlotState SlotStates[NumVisualSlots];

	TWeakObjectPtr<USkeletalMeshComponent> VisualBody;

	/** Body material slots currently hidden by worn pieces. */
	TSet<int32> AppliedBodyHidden;

	bool bHairHidden = false;
	bool bHairStateValid = false;

	FTimerHandle RefreshTimer;
};
