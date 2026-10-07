// Blackwood Hollow - crab Behavior Tree decorators
// Target: Unreal Engine 5.8 (C++)
//
//   Target Is Guarding ....... true while the crab's target is Blocking or Parrying (re-evaluated every tick, so it can abort).
//   Target Started Combo ..... true for Window seconds (default 0.4) after the target begins an attack (re-evaluated every tick).
//   Sidestep Gate ............ rolls Chance (default 35%) once per evaluation, requires Cooldown (default 4 s) since the crab's last
//                              sidestep and that the crab is not attacking / staggered / posture broken / dead. Not ticking: put it
//                              on a sequence entry with FlowAbortMode None.
// All three read ABH_CrabAIController (the controller must be a crab controller or the decorator is false).

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTDecorator.h"
#include "BTDecorator_BH_CrabConditions.generated.h"

class ABH_CrabAIController;

struct FBTBHCrabConditionMemory
{
	bool bLastResult = false;
};

/** Base for decorators that poll a condition each tick and request a flow abort when it flips. */
UCLASS(Abstract)
class BLACKWOODHOLLOWBETA_API UBTDecorator_BH_CrabConditionBase : public UBTDecorator
{
	GENERATED_BODY()

public:
	UBTDecorator_BH_CrabConditionBase(const FObjectInitializer& ObjectInitializer);

	virtual uint16 GetInstanceMemorySize() const override { return sizeof(FBTBHCrabConditionMemory); }
	virtual void InitializeMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryInit::Type InitType) const override;

protected:
	virtual bool CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const override;
	virtual void OnBecomeRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;

	/** Override: the condition itself (before inversion). Controller is never null. */
	virtual bool EvaluateCrabCondition(UBehaviorTreeComponent& OwnerComp, const ABH_CrabAIController& Controller) const { return false; }
};

UCLASS(meta = (DisplayName = "BH Target Is Guarding"))
class BLACKWOODHOLLOWBETA_API UBTDecorator_BH_TargetIsGuarding : public UBTDecorator_BH_CrabConditionBase
{
	GENERATED_BODY()

public:
	UBTDecorator_BH_TargetIsGuarding(const FObjectInitializer& ObjectInitializer);
	virtual FString GetStaticDescription() const override;

protected:
	virtual bool EvaluateCrabCondition(UBehaviorTreeComponent& OwnerComp, const ABH_CrabAIController& Controller) const override;
};

UCLASS(meta = (DisplayName = "BH Target Started Combo"))
class BLACKWOODHOLLOWBETA_API UBTDecorator_BH_TargetStartedCombo : public UBTDecorator_BH_CrabConditionBase
{
	GENERATED_BODY()

public:
	UBTDecorator_BH_TargetStartedCombo(const FObjectInitializer& ObjectInitializer);
	virtual FString GetStaticDescription() const override;

	/** Seconds after the target starts an attack during which the condition stays true. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition", meta = (ClampMin = "0.0"))
	float Window = 0.4f;

protected:
	virtual bool EvaluateCrabCondition(UBehaviorTreeComponent& OwnerComp, const ABH_CrabAIController& Controller) const override;
};

UCLASS(meta = (DisplayName = "BH Sidestep Gate"))
class BLACKWOODHOLLOWBETA_API UBTDecorator_BH_SidestepGate : public UBTDecorator
{
	GENERATED_BODY()

public:
	UBTDecorator_BH_SidestepGate(const FObjectInitializer& ObjectInitializer);
	virtual FString GetStaticDescription() const override;

	/** Probability (0..1) that the gate opens when evaluated and everything else allows it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Chance = 0.35f;

	/** Minimum seconds between sidesteps (measured from ABH_EnemyCrab::MarkSidestepUsed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition", meta = (ClampMin = "0.0"))
	float Cooldown = 4.f;

protected:
	virtual bool CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const override;
};
