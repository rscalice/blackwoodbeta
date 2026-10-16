
// Blackwood Hollow - Player Menu character preview (Phase 13G-0)
// Target: Unreal Engine 5.8 (C++), UMG
//
// A LOCAL-ONLY actor (never replicated) holding a scene capture that renders the live player pawn's visual body (and its armor pieces)
// into a render target, which WBP_PlayerMenu shows in an image brush. The capture runs ONLY while SetPreviewActive(true): a manual
// CaptureScene() each tick, so a closed menu costs nothing. The view orbits the pawn; AddPreviewYaw rotates it (drag / stick).
//
// Only the visual actor's primitives (body, hair, armor pieces) are drawn (ShowOnly list), refreshed about twice a second so armor
// swaps show up. Spawn with SpawnPreview (returns null on a dedicated server).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BH_CharacterPreview.generated.h"

class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

UCLASS(NotBlueprintable)
class BLACKWOODHOLLOWBETA_API ABH_CharacterPreview : public AActor
{
	GENERATED_BODY()

public:
	ABH_CharacterPreview();

	/** Spawns a local preview for Pawn (null on a dedicated server or without a world). The caller owns the lifetime (Destroy when the menu closes). */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|CharacterPreview", meta = (WorldContext = "WorldContextObject"))
	static ABH_CharacterPreview* SpawnPreview(UObject* WorldContextObject, APawn* Pawn);

	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|CharacterPreview")
	void SetTargetPawn(APawn* NewPawn);

	/** True while capturing. Off = no tick, no rendering. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|CharacterPreview")
	void SetPreviewActive(bool bActive);

	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|CharacterPreview")
	bool IsPreviewActive() const { return bPreviewActive; }

	/** Rotates the view around the pawn (degrees; positive = turn right). */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|CharacterPreview")
	void AddPreviewYaw(float DeltaDegrees);

	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|CharacterPreview")
	void ResetPreviewYaw();

	/** The render target to put in the UI image brush (created on first use when none is assigned). */
	UFUNCTION(BlueprintPure, Category = "BlackwoodHollow|CharacterPreview")
	UTextureRenderTarget2D* GetRenderTarget() const { return ActiveRenderTarget; }

	/** Rebuilds the capture's ShowOnly list from the pawn's current visual body and armor pieces. */
	UFUNCTION(BlueprintCallable, Category = "BlackwoodHollow|CharacterPreview")
	void RefreshShowOnlyList();

	/** Optional asset (RT_BH_CharacterPreview). When null a transient 512x1024 HDR target is created. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|CharacterPreview")
	TObjectPtr<UTextureRenderTarget2D> RenderTarget;

	/** Distance from the focus point to the camera, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|CharacterPreview", meta = (ClampMin = "50"))
	float ArmLength = 260.f;

	/** Focus height above the pawn's origin, cm (the capsule centre is about the hips; raise to frame the chest). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|CharacterPreview")
	float FocusHeightOffset = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|CharacterPreview", meta = (ClampMin = "5", ClampMax = "120"))
	float FieldOfView = 30.f;

	/** Current orbit angle, degrees (0 = camera in front of the pawn). */
	UPROPERTY(BlueprintReadOnly, Category = "BlackwoodHollow|CharacterPreview")
	float PreviewYaw = 0.f;

	/** Seconds between ShowOnly list refreshes while active. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlackwoodHollow|CharacterPreview", meta = (ClampMin = "0.1"))
	float ShowOnlyRefreshInterval = 0.5f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BlackwoodHollow|CharacterPreview")
	TObjectPtr<USceneCaptureComponent2D> Capture;

protected:
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void UpdateCaptureTransform();
	void EnsureRenderTarget();

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> ActiveRenderTarget;

	TWeakObjectPtr<APawn> TargetPawn;

	bool bPreviewActive = false;
	float TimeSinceShowOnlyRefresh = 0.f;
};
