// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tasks/TaskPrivate.h"
#include "Tasks/Pipe.h"

namespace UE { namespace Tasks { namespace Private
{
	FTaskBase* FTaskBase::PushIntoPipe()
	{
		return GetPipe()->PushIntoPipe(*this);
	}

	void FTaskBase::StartPipeExecution()
	{
		if (GetPipe() != nullptr)
		{
			GetPipe()->ExecutionStarted();
		}
	}

	void FTaskBase::FinishPipeExecution()
	{
		if (GetPipe() != nullptr)
		{
			GetPipe()->ExecutionFinished();
		}
	}

	void FTaskBase::Close()
	{
		if (GetPipe() != nullptr)
		{
			GetPipe()->ClearTask(*this);
		}

		Subsequents.Close(
			[this](FTaskBase* Subsequent)
			{
				Subsequent->TryUnlock();
			}
		);

		while (TOptional<FTaskBase*> Prerequisite = Prerequisites.Dequeue())
		{
			Prerequisite.GetValue()->Release();
		}

		Release(); // release the reference accounted for nested tasks. the task can be destroyed as the result
	}

	thread_local FTaskBase* CurrentTask = nullptr;

	FTaskBase* FTaskBase::GetCurrentTask()
	{
		return CurrentTask;
	}

	FTaskBase* FTaskBase::ExchangeCurrentTask(FTaskBase* Task)
	{
		FTaskBase* PrevTask = CurrentTask;
		CurrentTask = Task;
		return PrevTask;
	}
}}}