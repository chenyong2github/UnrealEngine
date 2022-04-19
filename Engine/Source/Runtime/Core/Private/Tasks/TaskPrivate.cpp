// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tasks/TaskPrivate.h"
#include "Tasks/Pipe.h"

namespace UE::Tasks::Private
{
	FTaskBase* FTaskBase::TryPushIntoPipe()
	{
		return GetPipe()->PushIntoPipe(*this);
	}

	void FTaskBase::StartPipeExecution()
	{
		GetPipe()->ExecutionStarted();
	}

	void FTaskBase::FinishPipeExecution()
	{
		GetPipe()->ExecutionFinished();
	}

	void FTaskBase::ClearPipe()
	{
		GetPipe()->ClearTask(*this);
	}

	static thread_local FTaskBase* CurrentTask = nullptr;

	FTaskBase* GetCurrentTask()
	{
		return CurrentTask;
	}

	FTaskBase* ExchangeCurrentTask(FTaskBase* Task)
	{
		FTaskBase* PrevTask = CurrentTask;
		CurrentTask = Task;
		return PrevTask;
	}
}
