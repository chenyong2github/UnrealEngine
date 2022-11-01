// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameplayInteractionSendSlotEventTask.h"
#include "StateTreeExecutionContext.h"
#include "SmartObjectSubsystem.h"
#include "StateTreeLinker.h"
#include "VisualLogger/VisualLogger.h"

FGameplayInteractionSendSlotEventTask::FGameplayInteractionSendSlotEventTask()
{
	// No tick needed.
	bShouldCallTick = false;
	bShouldCopyBoundPropertiesOnTick = false;
}

bool FGameplayInteractionSendSlotEventTask::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(SmartObjectSubsystemHandle);

	// Copy properties on exit state if the event is sent then.
	bShouldCopyBoundPropertiesOnExitState = (Trigger == EGameplayInteractionTaskTrigger::OnExitState);

	return true;
}

EStateTreeRunStatus FGameplayInteractionSendSlotEventTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	if (Trigger == EGameplayInteractionTaskTrigger::OnEnterState)
	{
		USmartObjectSubsystem& SmartObjectSubsystem = Context.GetExternalData(SmartObjectSubsystemHandle);
		const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

		if (InstanceData.TargetSlot.IsValid())
		{
			// Send the event
			SmartObjectSubsystem.SendSlotEvent(InstanceData.TargetSlot, EventTag, Payload);
		}
		else
		{
			UE_VLOG_UELOG(Context.GetOwner(), LogStateTree, Error, TEXT("[GameplayInteractionSendSlotEventTask] Expected valid TargetSlot handle."));
		}
	}

	return EStateTreeRunStatus::Running;
}

void FGameplayInteractionSendSlotEventTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	const bool bLastStateFailed = Transition.CurrentRunStatus == EStateTreeRunStatus::Failed;

	if (Trigger == EGameplayInteractionTaskTrigger::OnExitState
		|| (bLastStateFailed && Trigger == EGameplayInteractionTaskTrigger::OnExitStateFailed)
		|| (!bLastStateFailed && Trigger == EGameplayInteractionTaskTrigger::OnExitStateSucceeded))
	{
		USmartObjectSubsystem& SmartObjectSubsystem = Context.GetExternalData(SmartObjectSubsystemHandle);
		const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

		if (InstanceData.TargetSlot.IsValid())
		{
			// Send the event
			SmartObjectSubsystem.SendSlotEvent(InstanceData.TargetSlot, EventTag, Payload);
		}
		else
		{
			UE_VLOG_UELOG(Context.GetOwner(), LogStateTree, Error, TEXT("[GameplayInteractionSendSlotEventTask] Expected valid TargetSlot handle."));
		}
	}
}
