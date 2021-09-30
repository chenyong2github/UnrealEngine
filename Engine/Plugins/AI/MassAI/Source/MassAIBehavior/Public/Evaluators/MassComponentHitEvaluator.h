// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassComponentHitSubsystem.h"
#include "MassStateTreeTypes.h"
#include "MassComponentHitEvaluator.generated.h"

/**
 * Evaluator to extract last hit from the MassComponentHitSubsystem and expose it for tasks and transitions
 */
USTRUCT(meta = (DisplayName = "Mass ComponentHit Eval"))
struct MASSAIBEHAVIOR_API FMassComponentHitEvaluator : public FMassStateTreeEvaluatorBase
{
	GENERATED_BODY()

protected:
	virtual void Evaluate(FStateTreeExecutionContext& Context, const EStateTreeEvaluationType EvalType, const float DeltaTime) override;

	UPROPERTY(meta=(BaseClass="MassComponentHitSubsystem"))
	FStateTreeExternalItemHandle ComponentHitSubsystemHandle;

	UPROPERTY(VisibleAnywhere, Category = Output)
	bool bGotHit = false;

	UPROPERTY(VisibleAnywhere, Category = Output)
	FMassEntityHandle LastHitEntity;
};
