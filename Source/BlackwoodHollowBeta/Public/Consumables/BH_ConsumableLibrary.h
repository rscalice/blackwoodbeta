// Blackwood Hollow - consumable helpers (Phase 11D)
// Target: Unreal Engine 5.8 (C++)
//
//   FindDefinition         item class -> FBH_ConsumableDefinition: a UBH_ConsumableData asset whose ItemClass matches wins, else the built-in
//                          Heartwood Sap / Warden's Incense defaults (balance.md section 9), else false.
//   RequestUseConsumable   the one entry point for "the local player wants to use this item" (radial wheel, debug command, future hotkey).
//                          Server / listen host: activates directly. Client: sends UBH_InteractorComponent::ServerUseConsumable.
//   ActivateOnServer       server: sends the Event.Consumable.Use gameplay event (payload OptionalObject = the item class) to the pawn's ASC,
//                          which activates UBH_GA_UseConsumable. The ability does all the validation (item count, state tags).
//   ApplyConsumableEffect  server: the effect that lands when the use finishes (Sap heal over time / Incense sanctuary).

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Consumables/BH_ConsumableTypes.h"
#include "BH_ConsumableLibrary.generated.h"

class APawn;
class AActor;
class UNarrativeItem;

UCLASS()
class BLACKWOODHOLLOWBETA_API UBH_ConsumableLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Definition for ItemClass (data asset first, then the built-in defaults). @return false for an item that is not a known consumable. */
	UFUNCTION(BlueprintPure, Category = "BH|Consumable")
	static bool FindDefinition(TSubclassOf<UNarrativeItem> ItemClass, FBH_ConsumableDefinition& OutDefinition);

	/** The built-in Heartwood Sap definition (balance.md: 1.2 s drink, 45% MaxHealth over 3 s). */
	UFUNCTION(BlueprintPure, Category = "BH|Consumable")
	static FBH_ConsumableDefinition MakeSapDefinition();

	/** The built-in Warden's Incense definition (balance.md: 0.8 s censer drop, 6 m, 8 s). */
	UFUNCTION(BlueprintPure, Category = "BH|Consumable")
	static FBH_ConsumableDefinition MakeIncenseDefinition();

	/**
	 * LOCAL PLAYER (or the server): ask to use one ItemClass. Returns false when nothing was sent (no pawn, no item in the local inventory).
	 * A true result only means the request was sent; the server can still refuse (stunned, dead, already consuming).
	 */
	UFUNCTION(BlueprintCallable, Category = "BH|Consumable")
	static bool RequestUseConsumable(APawn* Pawn, TSubclassOf<UNarrativeItem> ItemClass);

	/** SERVER. Fires the use event on Pawn's ASC. @return true when UBH_GA_UseConsumable was triggered. */
	UFUNCTION(BlueprintCallable, Category = "BH|Consumable")
	static bool ActivateOnServer(APawn* Pawn, TSubclassOf<UNarrativeItem> ItemClass);

	/** SERVER. Applies the effect of Definition for Avatar (heal over time on it, or a sanctuary spawned in front of it). @return true if applied / spawned. */
	UFUNCTION(BlueprintCallable, Category = "BH|Consumable")
	static bool ApplyConsumableEffect(AActor* Avatar, const FBH_ConsumableDefinition& Definition);
};
