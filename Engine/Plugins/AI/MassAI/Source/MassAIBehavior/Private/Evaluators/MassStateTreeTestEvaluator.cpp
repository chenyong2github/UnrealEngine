// Copyright Epic Games, Inc. All Rights Reserved.

#include "Evaluators/MassStateTreeTestEvaluator.h"
#include "Engine/Engine.h"
#include "StateTreeExecutionContext.h"
#include "VisualLogger/VisualLogger.h"


FMassStateTreeTestEvaluator::FMassStateTreeTestEvaluator()
	: Time(0.0f)
	, bSignal(false)
{
}

FMassStateTreeTestEvaluator::~FMassStateTreeTestEvaluator()
{
}

void FMassStateTreeTestEvaluator::EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition)
{
	Time = 0.0f;
	bSignal = false;
}

void FMassStateTreeTestEvaluator::Evaluate(FStateTreeExecutionContext& Context, const EStateTreeEvaluationType EvalType, const float DeltaTime)
{
	Time += DeltaTime;
	if (Period > 0.0f)
	{
		bSignal = (FMath::FloorToInt(Time / Period) & 1) ? true : false;
		Type = EStateTreeEvaluationType::PreSelect;
	}
}
