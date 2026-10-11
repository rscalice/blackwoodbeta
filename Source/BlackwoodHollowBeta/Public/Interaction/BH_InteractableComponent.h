// Blackwood Hollow - generic interactable (Phase 11C)
// Target: Unreal Engine 5.8 (C++)
//
// Put a UBH_InteractableComponent on any actor the player can interact with (loot container, harvest node, world pickup,
// later the respawn-point repair). It carries the static configuration (prompt texts, hold time, range, outline stencil) and
// registers itself with UBH_InteractionSubsystem so the local player's UBH_InteractorComponent can find it.
//
// Why not NarrativeInteraction: UNarrativeInteractionComponent lives on the PlayerController, picks its target with a camera line
// trace, keeps the hold timer private (nothing replicated for a progress ring) and outlines through an overlay material. The
// revive already uses a different scheme (local proximity scan, IA_Interact on a dynamically added mapping context, a server-validated
// hold that replicates its progress), so interactions reuse that scheme instead of adding a second input path.
//
// The OWNING ACTOR decides what an interaction means: implement IBH_InteractableOwner on it (all methods have defaults).
//   BH_CanInteract        both machines. false + empty reason = not offered at all; false + reason = shown greyed with the reason.
//   BH_OnInteractionCompleted  SERVER only, once the hold finished (instant interactions: right on press).
// Everything cosmetic here (outline stencil, prompt) is LOCAL; state changes happen on the server through the owner actor.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UObject/Interface.h"
#include "BH_InteractableComponent.generated.h"

class APawn;
class UPrimitiveComponent;
class UBH_InteractableComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBH_OnInteractableFocus, bool, bFocused);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBH_OnInteractableCompleted, APawn*, InteractingPawn);

UINTERFACE(MinimalAPI)
class UBH_InteractableOwner : public UInterface
{
	GENERATED_BODY()
};

/** Implemented by the actor that owns a UBH_InteractableComponent. */
class BLACKWOODHOLLOWBETA_API IBH_InteractableOwner
{
	GENERATED_BODY()

public:
	/** Both machines (the client uses its replicated view for the prompt, the server re-checks). false with an empty reason hides the prompt. */
	virtual bool BH_CanInteract(const APawn* InteractingPawn, FText& OutDenyReason) const
	{
		OutDenyReason = FText::GetEmpty();
		return true;
	}

	/** Action text for this player ("Open", "Search"). Empty = use the component's PromptAction. */
	virtual FText BH_GetPromptAction(const APawn* InteractingPawn) const { return FText::GetEmpty(); }

	/** SERVER only. The interaction finished (hold complete, or pressed for an instant one). */
	virtual void BH_OnInteractionCompleted(APawn* InteractingPawn) {}

	/** SERVER only. bh.Loot.ResetContainers: put the world state back to "fresh". */
	virtual void BH_DebugReset() {}
};

UCLASS(ClassGroup = (BlackwoodHollow), meta = (BlueprintSpawnableComponent))
class BLACKWOODHOLLOWBETA_API UBH_InteractableComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBH_InteractableComponent();

	/** The interactable component on Actor, or null. */
	UFUNCTION(BlueprintPure, Category = "BH|Interaction")
	static UBH_InteractableComponent* Find(const AActor* Actor);

	// -- Configuration ---------------------------------------------------------------------------

	/** Name line of the prompt ("Supply Crate"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Interaction")
	FText PromptName = NSLOCTEXT("BlackwoodHollow", "InteractableDefaultName", "Object");

	/** Action line of the prompt ("Open"). The owner can override it per player (BH_GetPromptAction). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Interaction")
	FText PromptAction = NSLOCTEXT("BlackwoodHollow", "InteractableDefaultAction", "Interact");

	/** Seconds the interact key must be held. 0 = instant. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Interaction", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float HoldSeconds = 0.f;

	/** The player must be within this distance of the closest point of the owner's bounds to see the prompt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Interaction", meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float InteractRange = 220.f;

	/** Extra distance the server allows on top of InteractRange (latency, a step while holding). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Interaction", meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float ServerRangeTolerance = 80.f;

	/** Custom depth stencil value written on the owner's meshes while this is the local player's focus. Must match StencilValue of MI_Interact_Outline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Interaction")
	int32 OutlineStencilValue = 2;

	/** Draw the outline while focused. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Interaction")
	bool bOutlineWhenFocused = true;

	// -- Queries ---------------------------------------------------------------------------------

	/** Asks the owner (IBH_InteractableOwner) whether InteractingPawn may interact. No interface on the owner = always true. */
	bool EvaluateCanInteract(const APawn* InteractingPawn, FText& OutDenyReason) const;

	/** Prompt action for this player (owner override, else PromptAction). */
	FText GetPromptActionFor(const APawn* InteractingPawn) const;

	/** Distance from FromLocation to the closest point of the owner's bounds. */
	float GetDistanceFrom(const FVector& FromLocation) const;

	/** A point on the owner to face-test against (bounds centre). */
	FVector GetFocusPoint() const;

	// -- Local focus (cosmetic) --------------------------------------------------------------------

	/** LOCAL. Turns the outline stencil on / off on the owner's meshes and fires OnFocusChanged. Called by UBH_InteractorComponent. */
	void SetLocalFocus(bool bFocused);

	bool IsLocallyFocused() const { return bLocallyFocused; }

	/** SERVER. Runs the owner's completion hook and broadcasts OnInteractionCompleted. Called by UBH_InteractorComponent. */
	void NotifyInteractionCompleted(APawn* InteractingPawn);

	/** SERVER. Forwards bh.Loot.ResetContainers to the owner. */
	void DebugReset();

	/** LOCAL. The local player started / stopped looking at this. */
	UPROPERTY(BlueprintAssignable, Category = "BH|Interaction")
	FBH_OnInteractableFocus OnFocusChanged;

	/** SERVER. The interaction completed for InteractingPawn. */
	UPROPERTY(BlueprintAssignable, Category = "BH|Interaction")
	FBH_OnInteractableCompleted OnInteractionCompleted;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** Primitive state before the outline touched it (restored on unfocus). */
	struct FSavedCustomDepth
	{
		TWeakObjectPtr<UPrimitiveComponent> Component;
		bool bRenderCustomDepth = false;
		int32 StencilValue = 0;
	};

	TArray<FSavedCustomDepth> SavedCustomDepth;
	bool bLocallyFocused = false;
};
