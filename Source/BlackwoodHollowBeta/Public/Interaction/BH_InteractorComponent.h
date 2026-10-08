// Blackwood Hollow - the player's side of interaction (Phase 11C)
// Target: Unreal Engine 5.8 (C++)
//
// Lives on ABH_CharacterBase (created in its constructor, so every player pawn has one; only a locally controlled PLAYER pawn ever scans).
//
// LOCAL (cosmetic, every 0.1 s): finds the best UBH_InteractableComponent in range and roughly in front of the camera, applies the
// outline stencil to it, shows the prompt (UBH_InteractPromptWidget) and, while the focus can actually be used, adds IMC_Interact
// (IA_Interact on E / gamepad X at priority 10, same trick as the revive context) so the key does not fall through to Shield Bash.
// A downed party member in revive range always wins: no interaction focus while FindReviveCandidate() is non-null.
//
// SERVER (authoritative): ServerBeginInteract / ServerEndInteract. The server re-validates range, living state and
// BH_CanInteract, runs the hold timer (HoldSeconds of the interactable), cancels on key release / leaving range / taking damage, replicates
// the hold progress to the owning client only (HoldPercent) and, when the hold completes, calls UBH_InteractableComponent::NotifyInteractionCompleted.
// The client never decides an outcome.
//
// Input: ABH_CharacterBase::SetupPlayerInputComponent binds ReviveInputAction (IA_Interact) to OnInteractInputPressed / Released as well
// as to the death component (the revive handler ignores the press when there is nobody to revive, this one ignores it when there is).

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "BH_InteractorComponent.generated.h"

class APawn;
class APlayerController;
class UBH_InteractableComponent;
class UBH_InteractPromptWidget;
class UEnhancedInputLocalPlayerSubsystem;
class UInputMappingContext;
class UMaterialInterface;
class UNarrativeItem;
class UPostProcessComponent;

/** LOCAL. The focused interactable changed, or whether it can be used right now changed. Focus is null when nothing is in reach. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FBH_OnInteractFocusChanged, UBH_InteractableComponent*, Focus, bool, bCanInteract);

/** LOCAL (owning client + listen host). Hold progress 0..1 of the interaction the server is running for this player; 0 when idle. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBH_OnInteractHoldProgress, float, Fraction);

/** LOCAL (owning client + listen host). The server put Quantity of ItemName into this player's inventory. For a pickup toast. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FBH_OnItemGranted, FText, ItemName, int32, Quantity);

UCLASS(ClassGroup = (BlackwoodHollow), meta = (BlueprintSpawnableComponent))
class BLACKWOODHOLLOWBETA_API UBH_InteractorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBH_InteractorComponent();

	static UBH_InteractorComponent* Find(const AActor* Actor);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// -- Configuration ---------------------------------------------------------------------------

	/** Seconds between local scans. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Interaction", meta = (ClampMin = "0.02", ForceUnits = "s"))
	float ScanInterval = 0.1f;

	/** Minimum dot(camera forward, direction to the interactable) to be focusable from farther than FacingIgnoreDistance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Interaction", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float FacingMinDot = 0.25f;

	/** Closer than this the facing test is skipped (standing on top of a crate still works). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Interaction", meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float FacingIgnoreDistance = 120.f;

	/** Cancel a running hold when the player loses health. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Interaction")
	bool bCancelHoldOnDamage = true;

	/** Mapping context holding IA_Interact (E, gamepad X). Added while there is a usable focus. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Interaction")
	TSoftObjectPtr<UInputMappingContext> InteractMappingContext = TSoftObjectPtr<UInputMappingContext>(FSoftObjectPath(TEXT("/Game/Input/IMC_Interact.IMC_Interact")));

	/** Must be strictly higher than the combat context (priority 1). Same value as the revive context: the two are never active together. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Interaction")
	int32 InteractMappingPriority = 10;

	/** Post-process outline material instance (instance of M_LockOn_Outline, stencil 2, gold). Soft: if it cannot be loaded there is simply no outline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Interaction")
	TSoftObjectPtr<UMaterialInterface> OutlineMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/BlackwoodHollow/VFX/Materials/MI_Interact_Outline.MI_Interact_Outline")));

	/** Prompt widget (child of UBH_InteractPromptWidget). Created only if this asset exists; the delegates below work without it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Interaction")
	TSoftClassPtr<UBH_InteractPromptWidget> PromptWidgetClass = TSoftClassPtr<UBH_InteractPromptWidget>(FSoftObjectPath(TEXT("/Game/BlackwoodHollow/UI/WBP_InteractPrompt.WBP_InteractPrompt_C")));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|Interaction")
	int32 PromptZOrder = 5;

	// -- Queries ---------------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "BH|Interaction")
	UBH_InteractableComponent* GetFocus() const { return FocusedInteractable.Get(); }

	/** LOCAL. Hold progress of the running interaction, 0..1. */
	UFUNCTION(BlueprintPure, Category = "BH|Interaction")
	float GetHoldProgress() const { return static_cast<float>(HoldPercent) / 100.f; }

	// -- Input (bound by ABH_CharacterBase) ---------------------------------------------------------

	void OnInteractInputPressed();
	void OnInteractInputReleased();

	// -- Server side ---------------------------------------------------------------------------------

	/** SERVER. Tells the owning player what they received (toast). Handles listen host and remote clients. */
	void NotifyItemGranted(const FText& ItemName, int32 Quantity);

	/** SERVER. Aborts a running hold (death, teleport...). */
	void CancelActiveInteraction();

	// -- Events ------------------------------------------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "BH|Interaction")
	FBH_OnInteractFocusChanged OnFocusChanged;

	UPROPERTY(BlueprintAssignable, Category = "BH|Interaction")
	FBH_OnInteractHoldProgress OnHoldProgressChanged;

	UPROPERTY(BlueprintAssignable, Category = "BH|Interaction")
	FBH_OnItemGranted OnItemGranted;

	// -- RPCs --------------------------------------------------------------------------------------------

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerBeginInteract(UBH_InteractableComponent* Target);

	UFUNCTION(Server, Reliable)
	void ServerEndInteract();

	UFUNCTION(Client, Reliable)
	void ClientItemGranted(const FText& ItemName, int32 Quantity);

	/** bh.Loot.Give: server grants Count of ItemName (alias Shard / Sap / Incense or a class path) to this player. Compiled out in Shipping. */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerDebugGiveItem(const FString& ItemName, int32 Count);

	/** bh.Loot.ResetContainers: server resets every interactable in the world (containers, harvest nodes, pickups). Compiled out in Shipping. */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerDebugResetLoot();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	// -- Local ---------------------------------------------------------------------------------------
	void ScanForFocus();
	void ApplyFocus(UBH_InteractableComponent* NewFocus, bool bNewCanInteract, const FText& NewDenyReason);
	bool IsLocalPlayerPawn() const;
	APlayerController* GetLocalPC() const;
	void RefreshPrompt();
	void EnsurePromptWidget();
	void SetOutlineActive(bool bActive);
	void UpdateInputContext(bool bWantContext);
	void AddInteractContext(UEnhancedInputLocalPlayerSubsystem* InputSubsystem);
	void RemoveInteractContext();

	UFUNCTION()
	void OnRep_HoldPercent();

	void SetHoldPercentServer(uint8 NewPercent);

	// -- Server --------------------------------------------------------------------------------------
	bool ValidateTarget(const UBH_InteractableComponent* Target) const;
	void EndActive(bool bCompleted);
	float ReadHealth() const;

	/** Local focus (cosmetic). */
	TWeakObjectPtr<UBH_InteractableComponent> FocusedInteractable;
	bool bFocusCanInteract = false;
	FText FocusDenyReason;
	bool bLocalHoldInput = false;

	FTimerHandle ScanTimer;

	UPROPERTY(Transient)
	TObjectPtr<UBH_InteractPromptWidget> PromptWidget;
	bool bTriedLoadPrompt = false;

	UPROPERTY(Transient)
	TObjectPtr<UPostProcessComponent> OutlinePPComp;
	bool bTriedLoadOutline = false;

	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> InteractContextSubsystem;
	TWeakObjectPtr<UInputMappingContext> InteractContextAsset;
	bool bInteractContextActive = false;
	bool bWarnedMissingContext = false;

	/** 0..100, replicated to the owning client only. */
	UPROPERTY(ReplicatedUsing = OnRep_HoldPercent, Transient)
	uint8 HoldPercent = 0;

	/** Server: the interaction being held. */
	TWeakObjectPtr<UBH_InteractableComponent> ActiveInteractable;
	float ActiveElapsed = 0.f;
	float ActiveLastHealth = 0.f;
};
