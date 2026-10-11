// Blackwood Hollow - Warden's Incense sanctuary (Phase 11D)
// Target: Unreal Engine 5.8 (C++), GAS
//
// A replicated ground zone dropped by the Warden's Incense censer (UBH_GA_UseConsumable -> UBH_ConsumableLibrary::ApplyConsumableEffect).
// For its lifetime (Duration, default 8 s) every living ALLY (combat team Players) inside Radius (default 6 m, the user included) gets:
//   * the Blight meter drained by BlightDrainPerSecond (default 50 / s, floors at 0),
//   * Blight Rot removed the moment they enter,
//   * Blight build-up received x (1 - BlightReductionFraction) (default 50%) through UBPC_HeartFragment::AddBlightReductionSource,
//   * PostureRegenRate x PostureRegenMultiplier (default 1.5 = +50%) through UBH_GE_WardenSanctuaryBuff (State.Status.WardenSanctuary).
// Leaving the circle (or the sanctuary ending / being destroyed) undoes the reduction and the buff immediately.
//
// AUTHORITY: membership is evaluated on the SERVER only, by a 0.2 s sphere overlap (no collision setup needed on the pawns); everything
// above is applied through server-side GAS / the Heart-Fragment, so it replicates by the normal paths. Radius and Duration replicate so
// every machine sizes the ring the same way; the actor destroys itself on the server (SetLifeSpan) and the destruction replicates.
//
// VISUAL (cosmetic, local on every machine): a flat warm-gold disc (engine cylinder, 2 cm thick) scaled to the radius. It is a PLACEHOLDER:
// make BP_WardenSanctuary (child of this class) and swap RingMesh / add Niagara there, or point FBH_ConsumableDefinition::SanctuaryClass at it.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayEffectTypes.h"
#include "TimerManager.h"
#include "BH_WardenSanctuary.generated.h"

class APawn;
class UStaticMeshComponent;
class USceneComponent;

UCLASS(Blueprintable)
class BLACKWOODHOLLOWBETA_API ABH_WardenSanctuary : public AActor
{
	GENERATED_BODY()

public:
	ABH_WardenSanctuary();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Zone radius in cm (balance.md: 6 m). Set it before FinishSpawning (UBH_ConsumableLibrary does); replicated. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_Radius, Category = "Sanctuary", meta = (ClampMin = "50.0", ExposeOnSpawn = "true", ForceUnits = "cm"))
	float Radius = 600.f;

	/** Seconds the zone lasts (balance.md: 8 s). Server-side lifetime; replicated for HUD / VFX. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Sanctuary", meta = (ClampMin = "0.5", ExposeOnSpawn = "true", ForceUnits = "s"))
	float Duration = 8.f;

	/** Blight meter points removed per second from every ally inside (balance.md: 50). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sanctuary|Effects", meta = (ClampMin = "0.0"))
	float BlightDrainPerSecond = 50.f;

	/** Fraction of incoming Blight build-up removed for allies inside (0.5 = build-up x 0.5). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sanctuary|Effects", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BlightReductionFraction = 0.5f;

	/** PostureRegenRate multiplier for allies inside (1.5 = +50%). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sanctuary|Effects", meta = (ClampMin = "1.0"))
	float PostureRegenMultiplier = 1.5f;

	/** Seconds between server membership / drain updates. Drain per update = BlightDrainPerSecond * this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sanctuary|Effects", meta = (ClampMin = "0.05", ForceUnits = "s"))
	float UpdateInterval = 0.2f;

	/** Ring colour: warm gold (the Incense look). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sanctuary|Visual")
	FLinearColor RingColor = FLinearColor(1.f, 0.62f, 0.12f, 1.f);

	/** Living allies currently inside (server only; empty on clients). */
	UFUNCTION(BlueprintPure, Category = "Sanctuary")
	int32 GetMemberCount() const { return Members.Num(); }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void OnRep_Radius();

	/** Sizes the placeholder disc to Radius and tints it (local, cosmetic). */
	void ApplyRingVisual();

	/** Server: membership refresh + Blight drain. */
	void UpdateMembers();
	void AddMember(APawn* Pawn);
	void RemoveMember(APawn* Pawn);
	void RemoveAllMembers();

	UPROPERTY(VisibleAnywhere, Category = "Sanctuary")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "Sanctuary")
	TObjectPtr<UStaticMeshComponent> RingMesh;

	/** Server: pawn -> its sanctuary buff handle. */
	TMap<TWeakObjectPtr<APawn>, FActiveGameplayEffectHandle> Members;

	FTimerHandle UpdateTimer;
};
