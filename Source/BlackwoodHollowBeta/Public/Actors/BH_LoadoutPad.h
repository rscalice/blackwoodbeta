// Blackwood Hollow - loadout pad (walk onto it to swap weapon kit)
// Target: Unreal Engine 5.8 (C++), Niagara + UMG
//
// A replicated floor pad for the loadout room in front of an arena. When a player-controlled pawn steps on it, the
// server calls UBH_LoadoutComponent::ApplyLoadoutPreset(Preset): the pawn's weapon slots are re-filled from the preset
// (re-using inventory items, granting only what is missing) and the stance switches to set A's stance. The replicated
// inventory + stance carry the change to remote clients, so it works on a listen server and for joined clients alike.
//
// Components
//   PadTrigger   UBoxComponent root (profile "Trigger"; overlap events). Server-only gameplay logic.
//   AmbientFX    UNiagaraComponent, NOT auto-activating: the pad FX only play when a player uses the pad (multicast, every
//                non-dedicated machine, stopped again after PadFXPlayTime). Asset comes from AmbientFXAsset (per instance / BP child).
//   LabelWidget  UWidgetComponent, ~250 cm above the pad. WORLD space (see below). Widget class is chosen in a BP child
//                or on the placed instance; contract with that widget: a UTextBlock named "LabelText" (preset name) and
//                an optional UTextBlock named "SubText" ("Set A: <stance>  |  Set B: <stance>"). Both are looked up with
//                GetWidgetFromName and every use is null-safe, so a widget with only one of them (or none) is fine.
//   PadMesh      optional UStaticMeshComponent (floor disc); no mesh by default, no collision.
//
// Label space: World, drawn at desired size, with a yaw-only billboard tick on every machine that renders (not on a
// dedicated server). World space keeps the label anchored over its own pad, scaled by distance and hidden by walls --
// with several pads in one room that stays readable, while Screen space would draw every label on top of everything at a
// fixed size and pile them up. The billboard tick is cheap (one yaw per frame) and stops while the pad is inactive.
//
// Replication / cosmetics
//   bPadActive (replicated, OnRep) drives collision and label visibility on EVERY machine (and switches the pad FX off).
//   MulticastPlayApplyFX (NetMulticast, Unreliable) spawns ApplyBurstFX at the pawn and plays the pad's AmbientFX once:
//   purely cosmetic, a lost packet is fine.
//   OnLoadoutApplied / K2_OnLoadoutApplied fire on the server after a successful apply.
//
// "Temporary" pads: the server binds OnWaveStarted on every entry of WaveSpawners; the first wave start calls
// SetPadActive(false) when bDeactivateWhenWavesStart is set, so the loadout room closes once the fight begins.
// SetPadActive(true) re-opens it later (e.g. between arena runs).
//
// Per-pawn ReuseCooldown stops a pawn straddling the pad edge from re-applying the preset every overlap.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Combat/BH_LoadoutComponent.h"
#include "BH_LoadoutPad.generated.h"

class UBoxComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class UStaticMeshComponent;
class UWidgetComponent;
class ABH_EnemyWaveSpawner;
class APawn;
class UPrimitiveComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBH_OnLoadoutApplied, APawn*, Pawn);

UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API ABH_LoadoutPad : public AActor
{
	GENERATED_BODY()

public:
	ABH_LoadoutPad();

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void OnConstruction(const FTransform& Transform) override;

	// -- Config ---------------------------------------------------------------------------

	/** Big label, e.g. "Sword & Shield / Dual Sword". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|LoadoutPad")
	FText PresetDisplayName;

	/** The kit applied on step-in: Main/Off for set A and B (same struct as the starter loadout). A's stance becomes active. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|LoadoutPad")
	TArray<FBH_StarterLoadoutEntry> Preset;

	/** Pad effect (applied to AmbientFX); plays only when a player uses the pad. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|LoadoutPad|FX")
	TObjectPtr<UNiagaraSystem> AmbientFXAsset;

	/** Seconds the pad's own effect stays on after a use (it is switched off again after this long). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|LoadoutPad|FX", meta = (ClampMin = "0.1"))
	float PadFXPlayTime = 2.0f;

	/** One-shot effect spawned at the pawn when the preset is applied (all machines). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|LoadoutPad|FX")
	TObjectPtr<UNiagaraSystem> ApplyBurstFX;

	/** Seconds before the same pawn can trigger this pad again. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|LoadoutPad", meta = (ClampMin = "0"))
	float ReuseCooldown = 1.5f;

	/** Switch the pad off for good (until SetPadActive(true)) when any of WaveSpawners starts a wave. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BH|LoadoutPad")
	bool bDeactivateWhenWavesStart = true;

	/** Spawners whose OnWaveStarted closes this pad. Pick them in the level (instance only). */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "BH|LoadoutPad")
	TArray<TObjectPtr<ABH_EnemyWaveSpawner>> WaveSpawners;

	// -- Control --------------------------------------------------------------------------

	/** Server only. Enables / disables the pad on every machine (collision, ambient FX, label). */
	UFUNCTION(BlueprintCallable, Category = "BH|LoadoutPad")
	void SetPadActive(bool bActive);

	UFUNCTION(BlueprintPure, Category = "BH|LoadoutPad")
	bool IsPadActive() const { return bPadActive; }

	// -- Events ---------------------------------------------------------------------------

	/** Server only. A pawn's kit was swapped by this pad. */
	UPROPERTY(BlueprintAssignable, Category = "BH|LoadoutPad")
	FBH_OnLoadoutApplied OnLoadoutApplied;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Blueprint hook, same moment as OnLoadoutApplied (server). */
	UFUNCTION(BlueprintImplementableEvent, Category = "BH|LoadoutPad", meta = (DisplayName = "On Loadout Applied"))
	void K2_OnLoadoutApplied(APawn* Pawn);

	/** Blueprint hook on every machine when the pad turns on / off (e.g. hide the floor mesh). */
	UFUNCTION(BlueprintImplementableEvent, Category = "BH|LoadoutPad", meta = (DisplayName = "On Pad Active Changed"))
	void K2_OnPadActiveChanged(bool bActive);

	/** Cosmetic: ApplyBurstFX at the pawn. Unreliable: a dropped burst only loses a visual. */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayApplyFX(APawn* Pawn);

	UFUNCTION()
	void OnRep_PadActive();

	/** Timer callback: switches the pad's own effect off again. */
	void StopPadFX();

private:
	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void HandleWaveStarted(int32 WaveIndex);

	/** Applies bPadActive to collision / FX / label / tick. Runs on every machine. */
	void ApplyPadActiveState();

	/** Writes PresetDisplayName and the "Set A | Set B" line into the label widget (null-safe). */
	void RefreshLabel();

	/** "DualSword" -> "Dual Sword", "SwordAndShield" -> "Sword & Shield", None -> "-". */
	static FString StanceDisplayName(FName Stance);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BH|LoadoutPad", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UBoxComponent> PadTrigger;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BH|LoadoutPad", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> PadMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BH|LoadoutPad", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UNiagaraComponent> AmbientFX;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BH|LoadoutPad", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UWidgetComponent> LabelWidget;

	UPROPERTY(ReplicatedUsing = OnRep_PadActive)
	bool bPadActive = true;

	FTimerHandle PadFXStopTimer;

	/** Server: last successful/attempted use per pawn (world seconds). */
	TMap<TWeakObjectPtr<APawn>, double> LastUseTimes;
};
