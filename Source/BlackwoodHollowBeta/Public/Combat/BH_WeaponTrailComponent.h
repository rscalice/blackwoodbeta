// Blackwood Hollow - cosmetic weapon swing trail (procedural ribbon)
// Target: Unreal Engine 5.8 (C++)

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Combat/BH_WeaponTypes.h"
#include "BH_WeaponTrailComponent.generated.h"

class UANS_MeleeHitbox;
class UMaterialInterface;
class UProceduralMeshComponent;
class USkeletalMeshComponent;

/**
 * One per character, created on demand by UANS_MeleeHitbox::NotifyBegin (every machine; montage notifies fire everywhere).
 * While a melee hitbox window is open for a weapon slot the component samples the blade each tick (same root/tip logic as the
 * hitbox, UANS_MeleeHitbox::GetBladeRootTip), keeps the last MaxSamples samples for Lifetime seconds, smooths them with a
 * Catmull-Rom spline (<= MaxSegmentLength cm per segment) and rebuilds a world-space ribbon on a UProceduralMeshComponent.
 * UV.x = age (0 newest -> 1 oldest), UV.y = root -> tip. After the window ends no samples are added, so the ribbon fades out
 * over Lifetime and the mesh is then cleared and hidden. At most 2 trails per character (one per weapon slot).
 * Material / lifetime: see ResolveTrailStyle (identity override > Echo overlay > stance tint on DA_CombatFeel).
 */
UCLASS(ClassGroup = (BlackwoodHollow))
class BLACKWOODHOLLOWBETA_API UBH_WeaponTrailComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBH_WeaponTrailComponent();

	/** The trail component on Owner, creating (and registering) it if needed. Null outside game worlds / dedicated servers. */
	static UBH_WeaponTrailComponent* FindOrCreate(AActor* Owner);

	void BeginTrail(USkeletalMeshComponent* MeshComp, const UANS_MeleeHitbox* Hitbox);
	void EndTrail(EBH_WeaponSlot Slot);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Max retained samples per trail. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trail", meta = (ClampMin = "4", ClampMax = "64"))
	int32 MaxSamples = 20;

	/** Spline subdivision target (cm per segment). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trail", meta = (ClampMin = "1.0"))
	float MaxSegmentLength = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trail", meta = (ClampMin = "1", ClampMax = "16"))
	int32 MaxSubdivisions = 8;

	/** The ribbon starts this far along the blade (0 = hilt, 1 = tip) so it does not smear the grip. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trail", meta = (ClampMin = "0.0", ClampMax = "0.9"))
	float RootFraction = 0.25f;

	/** Debug / verification: one line per slot with state, material, vertex count, max vertex count, visibility. */
	UFUNCTION(BlueprintCallable, Category = "Trail")
	FString DescribeTrails() const;

private:
	struct FSample
	{
		FVector Root = FVector::ZeroVector;
		FVector Tip = FVector::ZeroVector;
		double Time = 0.0;
	};

	struct FChannel
	{
		bool bActive = false;
		bool bVisible = false;
		TWeakObjectPtr<USkeletalMeshComponent> Source;
		EBH_WeaponSlot Slot = EBH_WeaponSlot::MainHand;
		FName RootSocket;
		FName TipSocket;
		bool bUseBounds = true;
		float Lifetime = 0.12f;
		FName StyleKey;
		TArray<FSample> Samples;
		TObjectPtr<UProceduralMeshComponent> Ribbon = nullptr;
		TObjectPtr<UMaterialInterface> Material = nullptr;
		int32 VertexCount = 0;
		int32 MaxVertexCount = 0;
	};

	FChannel Channels[2];

	void ResolveTrailStyle(AActor* Owner, FChannel& Channel) const;
	void EnsureRibbon(FChannel& Channel);
	void AddSample(FChannel& Channel, double Now) const;
	void RebuildRibbon(FChannel& Channel, double Now);
	void HideRibbon(FChannel& Channel);
	void UpdateTickEnabled();

	// Scratch buffers (reused every rebuild).
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FColor> Colors;
};
