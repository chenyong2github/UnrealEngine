// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/ThreadSafeBool.h"
#include "HAL/ThreadSafeCounter64.h"
#include "HAL/Event.h"
#include "Containers/Queue.h"
#include "Containers/Array.h"
#include "Misc/Variant.h"
#include "AppEventHandler.h"
#include "Lumin/CAPIShims/LuminAPI.h"

struct FMagicLeapTask
{
	bool bSuccess;

	FMagicLeapTask()
	: bSuccess(false)
	{
	}
};

template <class TTaskType>
class FMagicLeapRunnable : public FRunnable
{
public:
	FMagicLeapRunnable(const TArray<EMagicLeapPrivilege>& InRequiredPrivileges, const FString& InName, EThreadPriority InThreadPriority = EThreadPriority::TPri_BelowNormal)
	: AppEventHandler(InRequiredPrivileges)
	, Name(InName)
	, Thread(nullptr)
	, ThreadPriority(InThreadPriority)
	, StopTaskCounter(0)
	, Semaphore(nullptr)
	, bPaused(false)
	{
		AppEventHandler.SetOnAppPauseHandler([this]() 
		{
			OnAppPause();
		});

		AppEventHandler.SetOnAppResumeHandler([this]()
		{
			OnAppResume();
		});

		AppEventHandler.SetOnAppShutDownHandler([this]()
		{
			OnAppShutDown();
		});
	}

	virtual ~FMagicLeapRunnable()
	{
		StopTaskCounter.Increment();

		if (Semaphore)
		{
			Semaphore->Trigger();
			Thread->WaitForCompletion();
			FGenericPlatformProcess::ReturnSynchEventToPool(Semaphore);
			Semaphore = nullptr;
		}

		delete Thread;
		Thread = nullptr;
	}

	uint32 Run() override
	{
		while (StopTaskCounter.GetValue() == 0)
		{
			if (bPaused)
			{
				Pause();
				// Cancel any incoming tasks.
				CancelIncomingTasks();
				// Wait for signal from resume call.
				Semaphore->Wait();
				Resume();
			}
			else if (!IncomingTasks.IsEmpty())
			{
				DoNextTask();
			}
			else
			{
				Semaphore->Wait();
			}
		}

		return 0;
	}

	void OnAppPause()
	{
		bPaused = true;
		if (Semaphore)
		{
			Semaphore->Trigger();
		}
	}

	void OnAppResume()
	{
		bPaused = false;
		if (Semaphore)
		{
			Semaphore->Trigger();
		}
	}

	void OnAppShutDown()
	{
		Stop();
	}

	void PushNewTask(TTaskType InTask)
	{
		// on demand thread creation
		if (!Thread)
		{
			Semaphore = FGenericPlatformProcess::GetSynchEventFromPool(false);
			Thread = FRunnableThread::Create(this, *Name, 0, ThreadPriority, FPlatformAffinity::GetPoolThreadMask());
		}

		IncomingTasks.Enqueue(InTask);
		// wake up the worker to process the task
		Semaphore->Trigger();
	}

	void PushCompletedTask(TTaskType InTask)
	{
		CompletedTasks.Enqueue(InTask);
	}

	bool TryGetCompletedTask(TTaskType& OutCompletedTask)
	{
#if PLATFORM_LUMIN
		if (CompletedTasks.Peek(OutCompletedTask))
		{
			CompletedTasks.Pop();
			return true;
		}
#endif //PLATFORM_LUMIN
		return false;
	}

protected:
	void CancelIncomingTasks()
	{
		while (IncomingTasks.Dequeue(CurrentTask))
		{
			CurrentTask.bSuccess = false;
			CompletedTasks.Enqueue(CurrentTask);
		}
	}

	virtual bool ProcessCurrentTask() = 0;
	virtual void Pause() {}
	virtual void Resume() {}

	/** Internal thread this instance is running on */
	MagicLeap::IAppEventHandler AppEventHandler;
	FString Name;
	FRunnableThread* Thread;
	EThreadPriority ThreadPriority;
	FThreadSafeCounter StopTaskCounter;
	FEvent* Semaphore;
	FThreadSafeBool bPaused;
	TQueue<TTaskType, EQueueMode::Spsc> IncomingTasks;
	TQueue<TTaskType, EQueueMode::Spsc> CompletedTasks;
	TTaskType CurrentTask;

private:
	bool DoNextTask()
	{
		IncomingTasks.Dequeue(CurrentTask);
		CurrentTask.bSuccess = ProcessCurrentTask();

		if (bPaused)
		{
			return false;
		}

		CompletedTasks.Enqueue(CurrentTask);

		return CurrentTask.bSuccess;
	}
};
