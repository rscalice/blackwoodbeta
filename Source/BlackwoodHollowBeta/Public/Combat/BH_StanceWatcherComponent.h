// Blackwood Hollow - stance (GASP overlay pose) watcher
// Target: Unreal Engine 5.8 (C++)
//
// GASP's CBP_SandboxCharacter applies the replicated OverlayPose through a plain (non-overridable)
// Blueprint function, so nothing of ours runs when a client receives a new pose. This component sits
// on the character on EVERY machine, watches the replicated pose a few times a second and re-attaches
// the weapon meshes for it, so a stance change looks the same everywhere.
//
// It also carries the stance-change request: RequestStance() applies the pose directly where the
// character has authority, otherwise asks the server through a reliable RPC (the pawn is owned by the
// requesting client's connection, so the RPC is accepted).

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BH_StanceWatcherComponent.generated.h"

class UBH_WeaponLoadoutDataAsset;

UCLASS(ClassGroup = (BlackwoodHollow), meta = (BlueprintSpawnableComponent))
class BLACKWOODHOLLOWBETA_API UBH_StanceWatcherComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBH_StanceWatcherComponent();

	/**
	 * Used when the character has no "WeaponLoadouts" object variable of its own
	 * (CBP_BlackwoodHollow does; the variable wins when both exist).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|Stance")
	TObjectPtr<UBH_WeaponLoadoutDataAsset> FallbackLoadouts;

	/** Seconds between OverlayPose checks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|Stance", meta = (ClampMin = "0.02"))
	float PollInterval = 0.1f;

	/** Asks for StanceDisplayName (an Enum_OverlayPose display name) on this character, from any machine. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Stance")
	void RequestStance(const FString& StanceDisplayName);

	/** Re-attaches the weapons if the overlay pose changed since the last sync (or always, with bForce). */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|Stance")
	void SyncWeapons(bool bForce = false);

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|Stance")
	static UBH_StanceWatcherComponent* FindStanceWatcher(const AActor* Actor);

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(Server, Reliable)
	void Server_SetStance(const FString& StanceDisplayName);

private:
	const UBH_WeaponLoadoutDataAsset* ResolveLoadouts() const;

	FString LastSyncedPose;
};
