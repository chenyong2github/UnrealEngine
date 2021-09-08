// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlFlowTask.h"
#include "ControlFlows.h"

//////////////////////////
//FControlFlowTask
//////////////////////////

FControlFlowSubTaskBase::FControlFlowSubTaskBase(const FString& TaskName)
{

}

void FControlFlowSubTaskBase::Execute()
{
	TaskCompleteCallback.ExecuteIfBound();
}

void FControlFlowSubTaskBase::Cancel()
{
	TaskCancelledCallback.ExecuteIfBound();
}

//////////////////////////
//FControlFlowBranch
//////////////////////////

FControlFlowTask_Branch::FControlFlowTask_Branch(FControlFlowBranchDecider& BranchDecider, const FString& TaskName)
	: FControlFlowSubTaskBase(TaskName)
	, BranchDelegate(BranchDecider)
{

}

FSimpleDelegate& FControlFlowTask_Branch::QueueFunction(int32 BranchIndex, const FString& FlowNodeDebugName /*= TEXT("")*/)
{
	return GetOrAddBranch(BranchIndex)->QueueFunction(FlowNodeDebugName);
}

FControlFlowWaitDelegate& FControlFlowTask_Branch::QueueWait(int32 BranchIndex, const FString& FlowNodeDebugName /*= TEXT("")*/)
{
	return GetOrAddBranch(BranchIndex)->QueueWait(FlowNodeDebugName);
}

FControlFlowPopulator& FControlFlowTask_Branch::QueueControlFlow(int32 BranchIndex, const FString& TaskName /*= TEXT("")*/, const FString& FlowNodeDebugName /*= TEXT("")*/)
{
	return GetOrAddBranch(BranchIndex)->QueueControlFlow(TaskName, FlowNodeDebugName);
}

TSharedRef<FControlFlowTask_Branch> FControlFlowTask_Branch::QueueBranch(int32 BranchIndex, FControlFlowBranchDecider& BranchDecider, const FString& TaskName /*= TEXT("")*/, const FString& FlowNodeDebugName /*= TEXT("")*/)
{
	return GetOrAddBranch(BranchIndex)->QueueBranch(BranchDecider, TaskName, FlowNodeDebugName);
}

FControlFlowPopulator& FControlFlowTask_Branch::QueueLoop(int32 BranchIndex, FControlFlowLoopComplete& LoopCompleteDelgate, const FString& TaskName /*= TEXT("")*/, const FString& FlowNodeDebugName /*= TEXT("")*/)
{
	return GetOrAddBranch(BranchIndex)->QueueLoop(LoopCompleteDelgate, TaskName, FlowNodeDebugName);
}

void FControlFlowTask_Branch::HandleBranchCompleted()
{
	Branches.Reset();

	OnComplete().ExecuteIfBound();
}

void FControlFlowTask_Branch::HandleBranchCancelled()
{
	Branches.Reset();

	OnCancelled().ExecuteIfBound();
}

TSharedRef<FControlFlow> FControlFlowTask_Branch::GetOrAddBranch(int32 BranchIndex)
{
	if (!Branches.Contains(BranchIndex))
	{
		Branches.Add(BranchIndex, MakeShared<FControlFlow>());
	}

	return Branches[BranchIndex];
}

void FControlFlowTask_Branch::Execute()
{
	if (BranchDelegate.IsBound())
	{
		SelectedBranch = BranchDelegate.Execute();

		TSharedRef<FControlFlow> FlowToExecute = GetOrAddBranch(SelectedBranch);

		FlowToExecute->OnComplete().BindSP(SharedThis(this), &FControlFlowTask_Branch::HandleBranchCompleted);
		FlowToExecute->OnExecutedWithoutAnyNodes().BindSP(SharedThis(this), &FControlFlowTask_Branch::HandleBranchCompleted);
		FlowToExecute->OnCancelled().BindSP(SharedThis(this), &FControlFlowTask_Branch::HandleBranchCancelled);

		FlowToExecute->ExecuteFlow();
	}
	else
	{
		HandleBranchCompleted();
	}
}

void FControlFlowTask_Branch::Cancel()
{
	if (Branches.Contains(SelectedBranch) && Branches[SelectedBranch]->IsRunning())
	{
		Branches[SelectedBranch]->CancelFlow();
	}
	else
	{
		HandleBranchCancelled();
	}
}

//////////////////////////////////
//FControlFlowSimpleSubTask
//////////////////////////////////

FControlFlowSimpleSubTask::FControlFlowSimpleSubTask(const FString& TaskName, TSharedRef<FControlFlow> FlowOwner)
	: FControlFlowSubTaskBase(TaskName)
	, TaskFlow(FlowOwner)
{

}

void FControlFlowSimpleSubTask::Execute()
{
	if (TaskPopulator.IsBound() && GetTaskFlow().IsValid())
	{
		GetTaskFlow()->OnComplete().BindSP(SharedThis(this), &FControlFlowSimpleSubTask::CompletedSubTask);
		GetTaskFlow()->OnExecutedWithoutAnyNodes().BindSP(SharedThis(this), &FControlFlowSimpleSubTask::CompletedSubTask);
		GetTaskFlow()->OnCancelled().BindSP(SharedThis(this), &FControlFlowSimpleSubTask::CancelledSubTask);

		TaskPopulator.Execute(GetTaskFlow().ToSharedRef());

		ensureAlwaysMsgf(!GetTaskFlow()->IsRunning(), TEXT("Did you call ExecuteFlow() on a SubFlow? You don't need to."));

		GetTaskFlow()->ExecuteFlow();
	}
	else
	{
		UE_LOG(LogControlFlows, Error, TEXT("ControlFlow - Executed Sub Task (%s) without proper set up"), *GetTaskName());

		CompletedSubTask();
	}
}

void FControlFlowSimpleSubTask::Cancel()
{
	if (GetTaskFlow().IsValid() && GetTaskFlow()->IsRunning())
	{
		GetTaskFlow()->CancelFlow();
	}
	else
	{
		CancelledSubTask();
	}
}

void FControlFlowSimpleSubTask::CompletedSubTask()
{
	OnComplete().ExecuteIfBound();
}

void FControlFlowSimpleSubTask::CancelledSubTask()
{
	OnCancelled().ExecuteIfBound();
}

//////////////////////////////////
//FControlFlowLoop
//////////////////////////////////

FControlFlowTask_Loop::FControlFlowTask_Loop(FControlFlowLoopComplete& TaskCompleteDelegate, const FString& TaskName, TSharedRef<FControlFlow> FlowOwner)
	: FControlFlowSimpleSubTask(TaskName, FlowOwner)
	, TaskCompleteDecider(TaskCompleteDelegate)
{

}

void FControlFlowTask_Loop::Execute()
{
	if (GetTaskPopulator().IsBound() && TaskCompleteDecider.IsBound() && GetTaskFlow().IsValid())
	{
		if (TaskCompleteDecider.Execute())
		{
			CompletedLoop();
		}
		else
		{
			GetTaskFlow()->OnComplete().BindSP(SharedThis(this), &FControlFlowTask_Loop::CompletedLoop);
			GetTaskFlow()->OnExecutedWithoutAnyNodes().BindSP(SharedThis(this), &FControlFlowTask_Loop::CompletedLoop);
			GetTaskFlow()->OnCancelled().BindSP(SharedThis(this), &FControlFlowTask_Loop::CancelledLoop);

			GetTaskPopulator().Execute(GetTaskFlow().ToSharedRef());

			GetTaskFlow()->ExecuteFlow();
		}
	}
	else
	{
		UE_LOG(LogControlFlows, Error, TEXT("ControlFlow - Executed Loop (%s) without proper bound delegates"), *GetTaskName());

		CompletedLoop();
	}
}

void FControlFlowTask_Loop::Cancel()
{
	if (GetTaskFlow().IsValid() && GetTaskFlow()->IsRunning())
	{
		GetTaskFlow()->CancelFlow();
	}
	else
	{
		CancelledLoop();
	}
}

void FControlFlowTask_Loop::CompletedLoop()
{
	if (TaskCompleteDecider.IsBound() && !TaskCompleteDecider.Execute())
	{
		Execute();
	}
	else
	{
		OnComplete().ExecuteIfBound();
	}
}

void FControlFlowTask_Loop::CancelledLoop()
{
	OnCancelled().ExecuteIfBound();
}