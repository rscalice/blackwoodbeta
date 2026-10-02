// Blackwood Hollow - Dodge ability (implementation)

#include "AbilitySystem/Abilities/AH_GA_Dodge.h"
#include "AbilitySystem/BH_GameplayTags.h"
#include "AbilitySystem/BH_CombatFunctionLibrary.h"
#include "Combat/BH_CombatFeel.h"
#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Animation/AnimMontage.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"

UAnimMontage* FBH_DodgeMontageSet::Get(EBH_DodgeDirection Direction) const
{
	switch (Direction)
	{
	case EBH_DodgeDirection::Forward: return Forward;
	case EBH_DodgeDirection::Back:    return Back;
	case EBH_DodgeDirection::Left:    return Left;
	case EBH_DodgeDirection::Right:   return Right;
	}
	return nullptr;
}

UAH_GA_Dodge::UAH_GA_Dodge()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;

	FGameplayTagContainer DefaultAssetTags;
	DefaultAssetTags.AddTag(TAG_Ability_Combat_Dodge);
	SetAssetTags(DefaultAssetTags);

	ActivationOwnedTags.AddTag(TAG_State_Combat_Dodging);

	ActivationBlockedTags.AddTag(TAG_State_Combat_Staggered);
	ActivationBlockedTags.AddTag(TAG_State_Combat_PostureBroken);
	ActivationBlockedTags.AddTag(TAG_State_Combat_Dead);
	ActivationBlockedTags.AddTag(TAG_State_Combat_Dodging); // tiny re-dodge lockout: no spamming mid-roll

	// Souls-style: a dodge interrupts swings, guard and parry, and none of them can start mid-roll.
	CancelAbilitiesWithTag.AddTag(TAG_Ability_Combat_MeleeAttack);
	CancelAbilitiesWithTag.AddTag(TAG_Ability_Combat_Block);
	CancelAbilitiesWithTag.AddTag(TAG_Ability_Combat_Parry);
	BlockAbilitiesWithTag.AddTag(TAG_Ability_Combat_MeleeAttack);
	BlockAbilitiesWithTag.AddTag(TAG_Ability_Combat_Block);
	BlockAbilitiesWithTag.AddTag(TAG_Ability_Combat_Parry);

	StaminaCost = 20.f;
	bAllowStaminaOvercommit = false; // a dodge needs the full cost
}

EBH_DodgeDirection UAH_GA_Dodge::ResolveDodgeDirection(const ACharacter* Character, float DeadZone)
{
	if (!Character)
	{
		return EBH_DodgeDirection::Back;
	}

	FVector Input = Character->GetLastMovementInputVector();
	if (Input.SizeSquared2D() <= FMath::Square(DeadZone))
	{
		Input = Character->GetPendingMovementInputVector();
	}
	if (Input.SizeSquared2D() <= FMath::Square(DeadZone))
	{
		// Server side of a remote client: no control input here, but the replicated acceleration mirrors it.
		const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
		Input = Movement ? Movement->GetCurrentAcceleration().GetSafeNormal() : FVector::ZeroVector;
	}
	if (Input.SizeSquared2D() <= FMath::Square(DeadZone))
	{
		return EBH_DodgeDirection::Back; // no input: backstep
	}

	// Yaw of the input relative to where the character faces (0 = forward, +90 = right).
	const FVector Local = Character->GetActorRotation().UnrotateVector(Input.GetSafeNormal2D());
	const float AngleDegrees = FMath::RadiansToDegrees(FMath::Atan2(Local.Y, Local.X));

	if (FMath::Abs(AngleDegrees) <= 45.f)
	{
		return EBH_DodgeDirection::Forward;
	}
	if (FMath::Abs(AngleDegrees) >= 135.f)
	{
		return EBH_DodgeDirection::Back;
	}
	return AngleDegrees > 0.f ? EBH_DodgeDirection::Right : EBH_DodgeDirection::Left;
}

UAnimMontage* UAH_GA_Dodge::PickMontage(const AActor* Avatar, EBH_DodgeDirection Direction) const
{
	const FString Stance = UBH_CombatFunctionLibrary::GetCurrentOverlayPoseDisplayName(Avatar);
	const FBH_DodgeMontageSet* Set = Stance.IsEmpty() ? nullptr : DirectionalMontages.Find(FName(*Stance));
	if (!Set)
	{
		Set = &DefaultMontages;
	}

	if (UAnimMontage* Montage = Set->Get(Direction))
	{
		return Montage;
	}
	// Missing direction in this set: a roll in the closest direction beats no dodge at all.
	for (const EBH_DodgeDirection Fallback : { EBH_DodgeDirection::Back, EBH_DodgeDirection::Forward, EBH_DodgeDirection::Left, EBH_DodgeDirection::Right })
	{
		if (UAnimMontage* Montage = Set->Get(Fallback))
		{
			return Montage;
		}
	}
	return nullptr;
}

void UAH_GA_Dodge::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	const ACharacter* Character = Cast<ACharacter>(Avatar);

	const EBH_DodgeDirection Direction = ResolveDodgeDirection(Character, InputDeadZone);
	UAnimMontage* Montage = PickMontage(Avatar, Direction);
	if (!Montage)
	{
		UE_LOG(LogBHCombat, Warning, TEXT("%s: no dodge montage for the current stance (fill DirectionalMontages / DefaultMontages)."), *GetName());
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	UE_LOG(LogBHCombat, Log, TEXT("Dodge: %s -> %s (%s)"), *GetNameSafe(Avatar), *UEnum::GetValueAsString(Direction), *Montage->GetName());

	// Exhale vocal (cosmetic; the dodge slide SFX is a TODO: no asset yet).
	UBH_CombatFeelLibrary::PlayVoice(Avatar, EBH_VoiceCategory::Dodge);

	MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this, NAME_None, Montage, DodgePlayRate);
	MontageTask->OnCompleted.AddDynamic(this, &UAH_GA_Dodge::OnMontageFinished);
	MontageTask->OnBlendOut.AddDynamic(this, &UAH_GA_Dodge::OnMontageFinished);
	MontageTask->OnInterrupted.AddDynamic(this, &UAH_GA_Dodge::OnMontageInterrupted);
	MontageTask->OnCancelled.AddDynamic(this, &UAH_GA_Dodge::OnMontageInterrupted);
	MontageTask->ReadyForActivation();

	// Shorter, tamer roll: scale root-motion travel for the duration of the dodge (both machines run this ability).
	if (ACharacter* MutableCharacter = Cast<ACharacter>(Avatar))
	{
		MutableCharacter->SetAnimRootMotionTranslationScale(RootMotionTranslationScale);
		ScaledCharacter = MutableCharacter;
	}

	// i-frames: a loose tag held between IFrameStart and IFrameEnd (timers run on the server and on the predicting client).
	UWorld* World = GetWorld();
	if (World)
	{
		const float Rate = FMath::Max(DodgePlayRate, 0.1f);
		const float StartTime = IFrameStart / Rate;
		const float EndTime = IFrameEnd / Rate;
		if (StartTime > 0.f)
		{
			World->GetTimerManager().SetTimer(IFrameStartTimer, this, &UAH_GA_Dodge::BeginIFrames, StartTime, false);
		}
		else
		{
			BeginIFrames();
		}
		World->GetTimerManager().SetTimer(IFrameEndTimer, this, &UAH_GA_Dodge::EndIFrames, FMath::Max(EndTime, StartTime + KINDA_SMALL_NUMBER), false);
	}
}

void UAH_GA_Dodge::BeginIFrames()
{
	if (bIFramesActive)
	{
		return;
	}
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->AddLooseGameplayTag(TAG_State_Combat_Invulnerable);
		bIFramesActive = true;
	}
}

void UAH_GA_Dodge::EndIFrames()
{
	if (!bIFramesActive)
	{
		return;
	}
	bIFramesActive = false;
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->RemoveLooseGameplayTag(TAG_State_Combat_Invulnerable);
	}
}

void UAH_GA_Dodge::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	// Normal end AND cancel (a stagger / posture break / death mid-roll): never leave the invulnerability tag behind.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(IFrameStartTimer);
		World->GetTimerManager().ClearTimer(IFrameEndTimer);
	}
	EndIFrames();

	if (ACharacter* Scaled = ScaledCharacter.Get())
	{
		Scaled->SetAnimRootMotionTranslationScale(1.f);
	}
	ScaledCharacter.Reset();

	if (MontageTask)
	{
		MontageTask->OnCompleted.RemoveAll(this);
		MontageTask->OnBlendOut.RemoveAll(this);
		MontageTask->OnInterrupted.RemoveAll(this);
		MontageTask->OnCancelled.RemoveAll(this);
		MontageTask = nullptr;
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UAH_GA_Dodge::OnMontageFinished()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UAH_GA_Dodge::OnMontageInterrupted()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}
