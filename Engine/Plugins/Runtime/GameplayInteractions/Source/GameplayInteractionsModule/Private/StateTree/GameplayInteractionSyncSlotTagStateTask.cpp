// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameplayInteractionSyncSlotTagStateTask.h"
#include "StateTreeExecutionContext.h"
#include "SmartObjectSubsystem.h"
#include "StateTreeLinker.h"
#include "VisualLogger/VisualLogger.h"

#define ST_INTERACTION_LOG(Verbosity, Format, ...) UE_VLOG_UELOG(Context.GetOwner(), LogStateTree, Verbosity, TEXT("[%s] ") Format, *StaticStruct()->GetName(), ##__VA_ARGS__)
#define ST_INTERACTION_CLOG(Condition, Verbosity, Format, ...) UE_CVLOG_UELOG((Condition), Context.GetOwner(), LogStateTree, Verbosity, TEXT("[%s] ") Format, *StaticStruct()->GetName(), ##__VA_ARGS__)

FGameplayInteractionSyncSlotTagStateTask::FGameplayInteractionSyncSlotTagStateTask()
{
	// No tick needed.
	bShouldCallTick = false;
	// No need to update bound properties after enter state, we assume the slot does not change.
	bShouldCopyBoundPropertiesOnTick = false;
	bShouldCopyBoundPropertiesOnExitState = false;
}

bool FGameplayInteractionSyncSlotTagStateTask::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(SmartObjectSubsystemHandle);
	return true;
}

EStateTreeRunStatus FGameplayInteractionSyncSlotTagStateTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	USmartObjectSubsystem& SmartObjectSubsystem = Context.GetExternalData(SmartObjectSubsystemHandle);
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	
	InstanceData.OnEventHandle.Reset();

	// @todo: should validate this during compile.
	if (!TagToMonitor.IsValid() || !BreakEventTag.IsValid())
	{
		UE_VLOG_UELOG(Context.GetOwner(), LogStateTree, Error, TEXT("[GameplayInteractionSyncSlotTagStateTask] Expected valid TagToMonitor and BreakEventTag."));
		return EStateTreeRunStatus::Failed;
	}
	
	if (!InstanceData.TargetSlot.IsValid())
	{
		UE_VLOG_UELOG(Context.GetOwner(), LogStateTree, Error, TEXT("[GameplayInteractionSyncSlotTagStateTask] Expected valid TargetSlot handle."));
		return EStateTreeRunStatus::Failed;
	}

	FOnSmartObjectEvent* OnEventDelegate = SmartObjectSubsystem.GetSlotEventDelegate(InstanceData.TargetSlot);
	if (OnEventDelegate == nullptr)
	{
		UE_VLOG_UELOG(Context.GetOwner(), LogStateTree, Error, TEXT("[GameplayInteractionSyncSlotTagStateTask] Expected to find event delegate for the slot."));
		return EStateTreeRunStatus::Failed;
	}

	FStateTreeEventQueue& EventQueue = Context.GetEventQueue();
	
	// Check initial state
	const FSmartObjectSlotView SlotView = SmartObjectSubsystem.GetSlotView(InstanceData.TargetSlot);
	if (!SlotView.IsValid())
	{
		UE_VLOG_UELOG(Context.GetOwner(), LogStateTree, Error, TEXT("[GameplayInteractionSyncSlotTagStateTask] Expected valid slot view."));
		return EStateTreeRunStatus::Failed;
	}

	InstanceData.bBreakSignalled = false;

	// Check initial state.
	if (!SlotView.GetTags().HasTag(TagToMonitor))
	{
		UE_VLOG_UELOG(Context.GetOwner(), LogStateTree, VeryVerbose, TEXT("[GameplayInteractionSyncSlotTagStateTask] Sync state (initial): [%s] -> Event %s"), *TagToMonitor.ToString(), *BreakEventTag.ToString());

		// Signal the other slot to change.
		EventQueue.SendEvent(Context.GetOwner(), BreakEventTag);
		SmartObjectSubsystem.SendSlotEvent(InstanceData.TargetSlot, BreakEventTag);
		InstanceData.bBreakSignalled = true;
	}

	if (!InstanceData.bBreakSignalled)
	{
		InstanceData.OnEventHandle = OnEventDelegate->AddLambda([this, InstanceDataRef = Context.GetInstanceDataStructRef(*this), &EventQueue, SmartObjectSubsystem = &SmartObjectSubsystem, Owner = Context.GetOwner()](const FSmartObjectEventData& Data)
		{
			if (Data.Reason == ESmartObjectChangeReason::OnTagRemoved)
			{
				check(InstanceDataRef.IsValid());
				FInstanceDataType& InstanceData = *InstanceDataRef;

				if (!InstanceData.bBreakSignalled && Data.Tag.MatchesTag(TagToMonitor))
				{
					UE_VLOG_UELOG(Owner, LogStateTree, VeryVerbose, TEXT("[GameplayInteractionSyncSlotTagStateTask] Sync state: [%s] -> Event %s"), *TagToMonitor.ToString(), *BreakEventTag.ToString());

					SmartObjectSubsystem->SendSlotEvent(InstanceData.TargetSlot, BreakEventTag);
					EventQueue.SendEvent(Owner, BreakEventTag);
					InstanceData.bBreakSignalled = true;
				}
			}
		});
	}

	return EStateTreeRunStatus::Running;
}

void FGameplayInteractionSyncSlotTagStateTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	USmartObjectSubsystem& SmartObjectSubsystem = Context.GetExternalData(SmartObjectSubsystemHandle);
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	if (InstanceData.OnEventHandle.IsValid())
	{
		if (FOnSmartObjectEvent* OnEventDelegate = SmartObjectSubsystem.GetSlotEventDelegate(InstanceData.TargetSlot))
		{
			OnEventDelegate->Remove(InstanceData.OnEventHandle);
		}
	}
	InstanceData.OnEventHandle.Reset();

	if (!InstanceData.bBreakSignalled)
	{
		Context.SendEvent(BreakEventTag);
		SmartObjectSubsystem.SendSlotEvent(InstanceData.TargetSlot, BreakEventTag);
		InstanceData.bBreakSignalled = true;
	}
}
