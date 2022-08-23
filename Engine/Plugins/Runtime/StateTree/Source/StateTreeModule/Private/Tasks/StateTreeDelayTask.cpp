// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeDelayTask.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeLinker.h"

EStateTreeRunStatus FStateTreeDelayTask::EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const
{
	InstanceDataType& InstanceData = Context.GetInstanceData<InstanceDataType>(*this);

	InstanceData.Time = 0.f;
	
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FStateTreeDelayTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	InstanceDataType& InstanceData = Context.GetInstanceData<InstanceDataType>(*this);

	InstanceData.Time += DeltaTime;
	
	return InstanceData.Duration <= 0.0f ? EStateTreeRunStatus::Running : (InstanceData.Time < InstanceData.Duration ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Succeeded);
}
