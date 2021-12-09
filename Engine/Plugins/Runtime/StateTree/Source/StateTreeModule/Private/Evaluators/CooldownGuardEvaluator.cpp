// Copyright Epic Games, Inc. All Rights Reserved.

#include "Evaluators/CooldownGuardEvaluator.h"
#include "StateTreeExecutionContext.h"
#include "Engine/World.h"


bool FCooldownGuardEvaluator::Link(FStateTreeLinker& Linker)
{
	Linker.LinkInstanceDataProperty(DurationHandle, STATETREE_INSTANCEDATA_PROPERTY(FCooldownGuardEvaluatorInstanceData, Duration));
	Linker.LinkInstanceDataProperty(EndTimeHandle, STATETREE_INSTANCEDATA_PROPERTY(FCooldownGuardEvaluatorInstanceData, EndTime));
	Linker.LinkInstanceDataProperty(ActiveHandle, STATETREE_INSTANCEDATA_PROPERTY(FCooldownGuardEvaluatorInstanceData, bActive));

	return true;
}

void FCooldownGuardEvaluator::SetCoolDown(FStateTreeExecutionContext& Context) const
{
	const float Duration = Context.GetInstanceData(DurationHandle);
	float& EndTime = Context.GetInstanceData(EndTimeHandle);
	bool& bActive = Context.GetInstanceData(ActiveHandle);

	const float CurrentTime = Context.GetWorld()->GetTimeSeconds();

	EndTime = CurrentTime + FMath::Max(0.0f, Duration + FMath::RandRange(-RandomDeviation, RandomDeviation));
	bActive = CurrentTime < EndTime;
}

void FCooldownGuardEvaluator::EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const
{
	if (Activation == ECooldownGuardActivation::OnEnterState)
	{
		SetCoolDown(Context);
	}
}
 
void FCooldownGuardEvaluator::StateCompleted(FStateTreeExecutionContext& Context, const EStateTreeRunStatus CompletionStatus, const FStateTreeHandle CompletedState) const
{
	if (CompletionStatus == EStateTreeRunStatus::Failed
		&& (Activation == ECooldownGuardActivation::OnCompleted || Activation == ECooldownGuardActivation::OnFailed))
	{
		SetCoolDown(Context);
	}

	if (CompletionStatus == EStateTreeRunStatus::Succeeded
		&& (Activation == ECooldownGuardActivation::OnCompleted || Activation == ECooldownGuardActivation::OnSucceeded))
	{
		SetCoolDown(Context);
	}
}

void FCooldownGuardEvaluator::Evaluate(FStateTreeExecutionContext& Context, const EStateTreeEvaluationType EvalType, const float DeltaTime) const
{
	const float EndTime = Context.GetInstanceData(EndTimeHandle);
	bool& bActive = Context.GetInstanceData(ActiveHandle);

	if (bActive)
	{
		const float CurrentTime = Context.GetWorld()->GetTimeSeconds();
		bActive = CurrentTime < EndTime;
	}
}
