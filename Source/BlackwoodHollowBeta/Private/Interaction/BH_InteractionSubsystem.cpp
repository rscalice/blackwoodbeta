// Blackwood Hollow - per-world registry of interactables (implementation)

#include "Interaction/BH_InteractionSubsystem.h"
#include "Interaction/BH_InteractableComponent.h"

void UBH_InteractionSubsystem::Register(UBH_InteractableComponent* Interactable)
{
	if (Interactable)
	{
		Interactables.AddUnique(Interactable);
	}
}

void UBH_InteractionSubsystem::Unregister(UBH_InteractableComponent* Interactable)
{
	Interactables.RemoveAll([Interactable](const TWeakObjectPtr<UBH_InteractableComponent>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == Interactable;
	});
}
