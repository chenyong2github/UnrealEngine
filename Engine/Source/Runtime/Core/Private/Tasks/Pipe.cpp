// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tasks/Pipe.h"
#include "Misc/ScopeExit.h"
#include "Tasks/Task.h"

namespace UE { namespace Tasks
{
	bool FPipe::PushIntoPipe(Private::FTaskBase& Task)
	{
		// critical section: between exchanging tasks and adding the previous LastTask to prerequisites.
		// `LastTask` can be in the process of destruction. Before this it will try to clear itself from the pipe
		// we need to let it know we're still using it so destruction should wait until we finished
		PushingThreadsNum.fetch_add(1, std::memory_order_relaxed);
		ON_SCOPE_EXIT{ PushingThreadsNum.fetch_sub(1, std::memory_order_relaxed); };

		Private::FTaskBase* LastTask_Local = LastTask.exchange(&Task, std::memory_order_release);
		checkf(LastTask_Local != &Task, TEXT("Dependency cycle: adding itself as a prerequisite (or use after destruction)"));

		return LastTask_Local == nullptr || !LastTask_Local->SetSubsequent(Task);
	}

	void FPipe::ClearTask(Private::FTaskBase& Task)
	{
		// clears `Task` from the pipe if it's still the `LastTask`. if it's not, another task already has set itself as the `LastTask` 
		// and can be in the process of adding this `Task` as a prerequisite. We need to wait for this to complete before returning 
		// from the call as the `Task` can be destroyed immediately after this
		Private::FTaskBase* Task_Local = &Task;
		if (!LastTask.compare_exchange_strong(Task_Local, nullptr, std::memory_order_acquire, std::memory_order_relaxed))
		{
			while (PushingThreadsNum.load(std::memory_order_relaxed) != 0)
			{
			}
		}
	}
}}