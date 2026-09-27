// Blackwood Hollow - Combo window AnimNotifyState (implementation)

#include "Animation/ANS_ComboWindow.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequenceBase.h"

namespace ANS_ComboWindow_Private
{
	static void SendComboWindowEvent(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FGameplayTag& EventTag, bool bWindowOpen, float Duration)
	{
		AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr;
		UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Owner);
		if (!ASC)
		{
			return;
		}

		// Count is forced (not incremented) so an interrupted montage that
		// skips NotifyEnd can never leave the tag stuck at 2+.
		ASC->SetLooseGameplayTagCount(TAG_State_Combat_ComboWindow, bWindowOpen ? 1 : 0);

		FGameplayEventData Payload;
		Payload.EventTag = EventTag;
		Payload.Instigator = Owner;
		Payload.Target = Owner;
		Payload.OptionalObject = Animation;
		Payload.EventMagnitude = Duration;
		ASC->HandleGameplayEvent(EventTag, &Payload);
	}
}

UANS_ComboWindow::UANS_ComboWindow()
{
#if WITH_EDITORONLY_DATA
	NotifyColor = FColor(80, 200, 255);
#endif
}

void UANS_ComboWindow::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	ANS_ComboWindow_Private::SendComboWindowEvent(MeshComp, Animation, TAG_Event_Combat_ComboWindow_Open, true, TotalDuration);
}

void UANS_ComboWindow::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	ANS_ComboWindow_Private::SendComboWindowEvent(MeshComp, Animation, TAG_Event_Combat_ComboWindow_Close, false, 0.f);
	Super::NotifyEnd(MeshComp, Animation, EventReference);
}

FString UANS_ComboWindow::GetNotifyName_Implementation() const
{
	return TEXT("Combo Window");
}
