// Blackwood Hollow - generic interactable (implementation)

#include "Interaction/BH_InteractableComponent.h"
#include "Interaction/BH_InteractionSubsystem.h"
#include "Components/MeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"

UBH_InteractableComponent::UBH_InteractableComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false); // all state lives on the owner actor
}

UBH_InteractableComponent* UBH_InteractableComponent::Find(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UBH_InteractableComponent>() : nullptr;
}

void UBH_InteractableComponent::BeginPlay()
{
	Super::BeginPlay();

	if (UWorld* ComponentWorld = GetWorld())
	{
		if (UBH_InteractionSubsystem* Registry = ComponentWorld->GetSubsystem<UBH_InteractionSubsystem>())
		{
			Registry->Register(this);
		}
	}
}

void UBH_InteractableComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SetLocalFocus(false);

	if (UWorld* ComponentWorld = GetWorld())
	{
		if (UBH_InteractionSubsystem* Registry = ComponentWorld->GetSubsystem<UBH_InteractionSubsystem>())
		{
			Registry->Unregister(this);
		}
	}

	Super::EndPlay(EndPlayReason);
}

bool UBH_InteractableComponent::EvaluateCanInteract(const APawn* InteractingPawn, FText& OutDenyReason) const
{
	OutDenyReason = FText::GetEmpty();
	if (!InteractingPawn)
	{
		return false;
	}
	if (const IBH_InteractableOwner* OwnerInterface = Cast<IBH_InteractableOwner>(GetOwner()))
	{
		return OwnerInterface->BH_CanInteract(InteractingPawn, OutDenyReason);
	}
	return true;
}

FText UBH_InteractableComponent::GetPromptActionFor(const APawn* InteractingPawn) const
{
	if (const IBH_InteractableOwner* OwnerInterface = Cast<IBH_InteractableOwner>(GetOwner()))
	{
		const FText Override = OwnerInterface->BH_GetPromptAction(InteractingPawn);
		if (!Override.IsEmpty())
		{
			return Override;
		}
	}
	return PromptAction;
}

float UBH_InteractableComponent::GetDistanceFrom(const FVector& FromLocation) const
{
	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return TNumericLimits<float>::Max();
	}
	const FBox Bounds = OwnerActor->GetComponentsBoundingBox(/*bNonColliding*/ true);
	if (!Bounds.IsValid)
	{
		return static_cast<float>(FVector::Dist(FromLocation, OwnerActor->GetActorLocation()));
	}
	return static_cast<float>(FVector::Dist(FromLocation, Bounds.GetClosestPointTo(FromLocation)));
}

FVector UBH_InteractableComponent::GetFocusPoint() const
{
	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return FVector::ZeroVector;
	}
	const FBox Bounds = OwnerActor->GetComponentsBoundingBox(/*bNonColliding*/ true);
	return Bounds.IsValid ? Bounds.GetCenter() : OwnerActor->GetActorLocation();
}

void UBH_InteractableComponent::SetLocalFocus(bool bFocused)
{
	if (bFocused == bLocallyFocused)
	{
		return;
	}
	bLocallyFocused = bFocused;

	AActor* OwnerActor = GetOwner();
	const UWorld* ComponentWorld = GetWorld();
	if (OwnerActor && bOutlineWhenFocused && ComponentWorld && ComponentWorld->GetNetMode() != NM_DedicatedServer)
	{
		if (bFocused)
		{
			SavedCustomDepth.Reset();
			TInlineComponentArray<UMeshComponent*> Meshes;
			OwnerActor->GetComponents(Meshes);
			for (UMeshComponent* MeshComp : Meshes)
			{
				if (!MeshComp)
				{
					continue;
				}
				FSavedCustomDepth Saved;
				Saved.Component = MeshComp;
				Saved.bRenderCustomDepth = MeshComp->bRenderCustomDepth;
				Saved.StencilValue = MeshComp->CustomDepthStencilValue;
				SavedCustomDepth.Add(Saved);

				MeshComp->SetCustomDepthStencilValue(OutlineStencilValue);
				MeshComp->SetRenderCustomDepth(true);
			}
		}
		else
		{
			for (const FSavedCustomDepth& Saved : SavedCustomDepth)
			{
				if (UPrimitiveComponent* Prim = Saved.Component.Get())
				{
					Prim->SetRenderCustomDepth(Saved.bRenderCustomDepth);
					Prim->SetCustomDepthStencilValue(Saved.StencilValue);
				}
			}
			SavedCustomDepth.Reset();
		}
	}

	OnFocusChanged.Broadcast(bFocused);
}

void UBH_InteractableComponent::NotifyInteractionCompleted(APawn* InteractingPawn)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !InteractingPawn)
	{
		return;
	}
	if (IBH_InteractableOwner* OwnerInterface = Cast<IBH_InteractableOwner>(GetOwner()))
	{
		OwnerInterface->BH_OnInteractionCompleted(InteractingPawn);
	}
	OnInteractionCompleted.Broadcast(InteractingPawn);
}

void UBH_InteractableComponent::DebugReset()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	if (IBH_InteractableOwner* OwnerInterface = Cast<IBH_InteractableOwner>(GetOwner()))
	{
		OwnerInterface->BH_DebugReset();
	}
}
