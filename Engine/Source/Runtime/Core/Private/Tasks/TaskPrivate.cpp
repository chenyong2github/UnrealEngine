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

		verify(Subsequents.Close(
			[this](FTaskBase* Subsequent)
			{
				Subsequent->TryUnlock();
			}
		));

		// release nested tasks. this can happen concurrently with task retraction, that also consumes `Prerequisite`. 
		// `Prerequisites` is a single-producer/single-consumer queue so we need to put an aditional synchronisation for dequeueing
		{
			TScopeLock<FSpinLock> ScopeLock(PrerequisitesLock);
			while (TOptional<FTaskBase*> Prerequisite = Prerequisites.Dequeue())
			{
				TScopeUnlock<FSpinLock> ScopeUnlock(PrerequisitesLock); // only `Prerequisites.Dequeue()` needs to be locked
				Prerequisite.GetValue()->Release();
			}
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