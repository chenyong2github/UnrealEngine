// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassComponentHitSubsystem.h"
#include "MassStateTreeTypes.h"
#include "MassComponentHitEvaluator.generated.h"

class UMassComponentHitSubsystem;

/**
 * Evaluator to extract last hit from the MassComponentHitSubsystem and expose it for tasks and transitions
 */
USTRUCT(meta = (DisplayName = "Mass ComponentHit Eval"))
struct MASSAIBEHAVIOR_API FMassComponentHitEvaluator : public FMassStateTreeEvaluatorBase
{
	GENERATED_BODY()

protected:
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual void Evaluate(FStateTreeExecutionContext& Context, const EStateTreeEvaluationType EvalType, const float DeltaTime) override;

	TStateTreeItemHandle<UMassComponentHitSubsystem> ComponentHitSubsystemHandle;

	UPROPERTY(VisibleAnywhere, Category = Output)
	bool bGotHit = false;

	UPROPERTY(VisibleAnywhere, Category = Output)
	FMassEntityHandle LastHitEntity;
};
