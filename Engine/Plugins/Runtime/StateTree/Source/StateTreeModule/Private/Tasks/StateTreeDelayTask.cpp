// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeDelayTask.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeLinker.h"

bool FStateTreeDelayTask::Link(FStateTreeLinker& Linker)
{
	Linker.LinkInstanceDataProperty(DurationHandle, STATETREE_INSTANCEDATA_PROPERTY(InstanceDataType, Duration));
	Linker.LinkInstanceDataProperty(TimeHandle, STATETREE_INSTANCEDATA_PROPERTY(InstanceDataType, Time));
	return true;
}

EStateTreeRunStatus FStateTreeDelayTask::EnterState(FStateTreeExecutionContext& Context, const EStateTreeStateChangeType ChangeType, const FStateTreeTransitionResult& Transition) const
{
	float& Time = Context.GetInstanceData(TimeHandle);
	Time = 0.f;
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FStateTreeDelayTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	float& Time = Context.GetInstanceData(TimeHandle);
	const float Duration = Context.GetInstanceData(DurationHandle);
	Time += DeltaTime;
	
	return Duration <= 0.0f ? EStateTreeRunStatus::Running : (Time < Duration ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Succeeded);
}
