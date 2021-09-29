// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StateTreeExecutionContext.h"
#include "MassStateTreeTypes.h"
#include "MassStateTreeTestEvaluator.generated.h"

/**
 * Test Evaluator, will be removed later.
 */
USTRUCT(meta = (DisplayName = "Mass Test Eval"))
struct MASSAIBEHAVIOR_API FMassStateTreeTestEvaluator : public FMassStateTreeEvaluatorBase
{
	GENERATED_BODY()

	FMassStateTreeTestEvaluator();
	virtual ~FMassStateTreeTestEvaluator();
	virtual void EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) override;
	virtual void Evaluate(FStateTreeExecutionContext& Context, const EStateTreeEvaluationType EvalType, const float DeltaTime) override;

protected:
	UPROPERTY()
	float Time = 0.0f;

	UPROPERTY()
	bool bSignal = false;

	UPROPERTY(EditAnywhere, Category = Test, meta = (Bindable))
	EStateTreeEvaluationType Type = EStateTreeEvaluationType::Tick;
	
	UPROPERTY(EditAnywhere, Category = Test, meta = (Bindable))
	float Period = 5.0f;
};
