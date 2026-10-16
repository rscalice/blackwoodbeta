
// Blackwood Hollow - Player Menu character preview (Phase 13G-0, implementation)

#include "UI/BH_CharacterPreview.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SceneComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Items/BH_ArmorVisualComponent.h"
#include "Items/BH_EquipmentTypes.h"

ABH_CharacterPreview::ABH_CharacterPreview()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	bReplicates = false;
	SetCanBeDamaged(false);

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Capture"));
	Capture->SetupAttachment(Root);
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	Capture->bAlwaysPersistRenderingState = true;
	Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
	Capture->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDR;
	Capture->ShowFlags.SetAtmosphere(false);
	Capture->ShowFlags.SetFog(false);
}

ABH_CharacterPreview* ABH_CharacterPreview::SpawnPreview(UObject* WorldContextObject, APawn* Pawn)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ABH_CharacterPreview* Preview = World->SpawnActor<ABH_CharacterPreview>(ABH_CharacterPreview::StaticClass(), FTransform::Identity, Params);
	if (Preview)
	{
		Preview->SetTargetPawn(Pawn);
	}
	return Preview;
}

void ABH_CharacterPreview::EnsureRenderTarget()
{
	if (ActiveRenderTarget)
	{
		return;
	}
	if (RenderTarget)
	{
		ActiveRenderTarget = RenderTarget;
	}
	else
	{
		UTextureRenderTarget2D* NewTarget = NewObject<UTextureRenderTarget2D>(this);
		NewTarget->RenderTargetFormat = RTF_RGBA16f;
		NewTarget->ClearColor = FLinearColor::Transparent;
		NewTarget->InitAutoFormat(512, 1024);
		NewTarget->UpdateResourceImmediate(true);
		ActiveRenderTarget = NewTarget;
	}
	Capture->TextureTarget = ActiveRenderTarget;
}

void ABH_CharacterPreview::SetTargetPawn(APawn* NewPawn)
{
	TargetPawn = NewPawn;
	TimeSinceShowOnlyRefresh = ShowOnlyRefreshInterval; // refresh on the next capture
	if (bPreviewActive)
	{
		RefreshShowOnlyList();
	}
}

void ABH_CharacterPreview::SetPreviewActive(bool bActive)
{
	if (bPreviewActive == bActive)
	{
		return;
	}
	bPreviewActive = bActive;
	SetActorTickEnabled(bActive);
	if (bActive)
	{
		EnsureRenderTarget();
		RefreshShowOnlyList();
		UpdateCaptureTransform();
		Capture->CaptureScene();
	}
}

void ABH_CharacterPreview::AddPreviewYaw(float DeltaDegrees)
{
	PreviewYaw = FRotator::NormalizeAxis(PreviewYaw + DeltaDegrees);
}

void ABH_CharacterPreview::ResetPreviewYaw()
{
	PreviewYaw = 0.f;
}

void ABH_CharacterPreview::RefreshShowOnlyList()
{
	TimeSinceShowOnlyRefresh = 0.f;
	Capture->ClearShowOnlyComponents();

	const APawn* Pawn = TargetPawn.Get();
	if (!Pawn)
	{
		return;
	}
	const UBH_ArmorVisualComponent* Visuals = UBH_ArmorVisualComponent::Find(Pawn);
	const USkeletalMeshComponent* Body = Visuals ? Visuals->GetVisualBody() : nullptr;
	if (!Body)
	{
		return;
	}

	// Everything on the visual actor (body, hair grooms, armor pieces attached to it).
	if (const AActor* VisualActor = Body->GetOwner())
	{
		TInlineComponentArray<UPrimitiveComponent*> Prims(VisualActor);
		for (UPrimitiveComponent* Prim : Prims)
		{
			Capture->ShowOnlyComponent(Prim);
		}
	}
	// Armor pieces, in case any is parented elsewhere.
	for (const EBH_EquipSlot Slot : { EBH_EquipSlot::Head, EBH_EquipSlot::Chest, EBH_EquipSlot::Arms, EBH_EquipSlot::Legs, EBH_EquipSlot::Feet })
	{
		if (UPrimitiveComponent* Piece = Visuals->GetSlotComponent(Slot))
		{
			Capture->ShowOnlyComponent(Piece);
		}
	}
}

void ABH_CharacterPreview::UpdateCaptureTransform()
{
	const APawn* Pawn = TargetPawn.Get();
	if (!Pawn)
	{
		return;
	}
	const FVector Focus = Pawn->GetActorLocation() + FVector(0.f, 0.f, FocusHeightOffset);
	// Yaw 0 = camera in front of the pawn, looking back at it.
	const FRotator Orbit(0.f, Pawn->GetActorRotation().Yaw + PreviewYaw, 0.f);
	const FVector CamLoc = Focus + Orbit.RotateVector(FVector(ArmLength, 0.f, 0.f));
	Capture->SetWorldLocationAndRotation(CamLoc, (Focus - CamLoc).Rotation());
	Capture->FOVAngle = FieldOfView;
}

void ABH_CharacterPreview::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bPreviewActive || !ActiveRenderTarget)
	{
		return;
	}
	TimeSinceShowOnlyRefresh += DeltaSeconds;
	if (TimeSinceShowOnlyRefresh >= ShowOnlyRefreshInterval)
	{
		RefreshShowOnlyList();
	}
	UpdateCaptureTransform();
	Capture->CaptureScene();
}

void ABH_CharacterPreview::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bPreviewActive = false;
	Super::EndPlay(EndPlayReason);
}
