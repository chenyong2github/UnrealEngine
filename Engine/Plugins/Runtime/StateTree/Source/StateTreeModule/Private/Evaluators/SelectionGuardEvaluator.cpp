// Copyright Epic Games, Inc. All Rights Reserved.

#include "Evaluators/SelectionGuardEvaluator.h"
#include "StateTreeExecutionContext.h"


bool FSelectionGuardEvaluator::Link(FStateTreeLinker& Linker)
{
	Linker.LinkInstanceDataProperty(ActiveHandle, STATETREE_INSTANCEDATA_PROPERTY(FSelectionGuardEvaluatorInstanceData, bActive));

	return true;
}

void FSelectionGuardEvaluator::EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const
{
	if (Activation == ESelectionGuardActivation::OnEnterState)
	{
		bool& bActive = Context.GetInstanceData(ActiveHandle);
		bActive = true;
	}
}

void FSelectionGuardEvaluator::ExitState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const
{
	bool& bActive = Context.GetInstanceData(ActiveHandle);
	
	// Since new state selection happen _before_ ExitState() is called,
	// setting the bActive on ExitState() can be used to check
	// if the state is currently active and prevent re-selecting it.
	bActive = false;
}

void FSelectionGuardEvaluator::StateCompleted(FStateTreeExecutionContext& Context, const EStateTreeRunStatus CompletionStatus, const FStateTreeHandle CompletedState) const
{
	bool& bActive = Context.GetInstanceData(ActiveHandle);

	if (CompletionStatus == EStateTreeRunStatus::Failed
		&& (Activation == ESelectionGuardActivation::OnCompleted || Activation == ESelectionGuardActivation::OnFailed))
	{
		bActive = true;
	}

	if (CompletionStatus == EStateTreeRunStatus::Succeeded
		&& (Activation == ESelectionGuardActivation::OnCompleted || Activation == ESelectionGuardActivation::OnSucceeded))
	{
		bActive = true;
	}
}