// Blackwood Hollow - per-world registry of interactables (Phase 11C)
// Target: Unreal Engine 5.8 (C++)
//
// UBH_InteractableComponent registers here on BeginPlay and leaves on EndPlay. UBH_InteractorComponent scans the list a few times a
// second instead of iterating every actor. One registry per world, so a PIE host and its clients (separate worlds in one process)
// never see each other's actors.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "BH_InteractionSubsystem.generated.h"

class UBH_InteractableComponent;

UCLASS()
class BLACKWOODHOLLOWBETA_API UBH_InteractionSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override
	{
		return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
	}

	void Register(UBH_InteractableComponent* Interactable);
	void Unregister(UBH_InteractableComponent* Interactable);

	const TArray<TWeakObjectPtr<UBH_InteractableComponent>>& GetAll() const { return Interactables; }

private:
	TArray<TWeakObjectPtr<UBH_InteractableComponent>> Interactables;
};
