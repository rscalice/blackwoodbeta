// Blackwood Hollow - Combo window AnimNotifyState
// Target: Unreal Engine 5.8 (C++), GAS (GameplayAbilities plugin required)

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "ANS_ComboWindow.generated.h"

/**
 * UANS_ComboWindow
 *
 * Place over the frames of an attack montage section where the NEXT attack in
 * the combo may be queued. While active it:
 *   - sends Event.Combat.ComboWindow.Open to the owner's ASC on begin and
 *     Event.Combat.ComboWindow.Close on end (UAH_GA_MeleeAttack_Base listens
 *     for both and uses them to accept / consume buffered attack input), and
 *   - holds the loose tag State.Combat.ComboWindow (local only, not replicated)
 *     so Blueprints / other abilities can query it.
 *
 * Silently does nothing on actors without an AbilitySystemComponent (e.g. the
 * animation editor preview actor).
 */
UCLASS(meta = (DisplayName = "BH Combo Window"))
class BLACKWOODHOLLOWBETA_API UANS_ComboWindow : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	UANS_ComboWindow();

	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration,
		const FAnimNotifyEventReference& EventReference) override;

	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

	virtual FString GetNotifyName_Implementation() const override;
};
