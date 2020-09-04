// Copyright Epic Games, Inc. All Rights Reserved.

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
#include "MagicLeapTypes.h"
#include "Lumin/CAPIShims/LuminAPI.h"

struct FMagicLeapTask
{
	bool bSuccess;
	FMagicLeapResult Result;

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
	, bShuttingDown(false)
	{
		AppEventHandler.SetOnAppPauseHandler([this]() 
		{
			OnAppPause();
		});

		AppEventHandler.SetOnAppResumeHandler([this]()
		{
			OnAppResume();
		});

		AppEventHandler.SetOnAppStartHandler([this]()
		{
			OnAppStart();
		});

		AppEventHandler.SetOnAppShutDownHandler([this]()
		{
			OnAppShutdown();
		});
	}

	virtual ~FMagicLeapRunnable()
	{
		Stop();
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

	virtual void Start()
	{
		StopTaskCounter.Reset();
		Semaphore = FGenericPlatformProcess::GetSynchEventFromPool(false);
		Thread = FRunnableThread::Create(this, *Name, 0, ThreadPriority, FPlatformAffinity::GetPoolThreadMask());
	}

	void Stop() override
	{
		StopTaskCounter.Increment();

		if (Semaphore)
		{
			Semaphore->Trigger();
			Thread->WaitForCompletion();
			FGenericPlatformProcess::ReturnSynchEventToPool(Semaphore);
			Semaphore = nullptr;
			delete Thread;
			Thread = nullptr;
		}
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

	void OnAppStart()
	{
		bShuttingDown = false;
	}

	void OnAppShutdown()
	{
		bShuttingDown = true;
	}

	void PushNewTask(const TTaskType& InTask)
	{
		if (bShuttingDown)
		{
			return;
		}

		// on demand thread creation
		if (!Thread)
		{
			Start();
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
		if (CompletedTasks.Peek(OutCompletedTask))
		{
			CompletedTasks.Pop();
			return true;
		}
		return false;
	}

	bool IsRunning() const
	{
		return StopTaskCounter.GetValue() == 0;
	}

protected:
	void CancelIncomingTasks()
	{
		while (IncomingTasks.Dequeue(CurrentTask))
		{
			CurrentTask.Result.bSuccess = CurrentTask.bSuccess = false;
			CompletedTasks.Enqueue(CurrentTask);
		}
	}

	virtual bool ProcessCurrentTask() = 0;
	virtual void Pause() {}
	virtual void Resume() {}

	MagicLeap::IAppEventHandler AppEventHandler;
	FString Name;
	/** Internal thread this instance is running on */
	FRunnableThread* Thread;
	EThreadPriority ThreadPriority;
	FThreadSafeCounter StopTaskCounter;
	FEvent* Semaphore;
	FThreadSafeBool bPaused;
	FThreadSafeBool bShuttingDown;
	TQueue<TTaskType, EQueueMode::Spsc> IncomingTasks;
	TQueue<TTaskType, EQueueMode::Mpsc> CompletedTasks; // allow multiple (binder) threads to push completed tasks
	TTaskType CurrentTask;

private:
	bool DoNextTask()
	{
		IncomingTasks.Dequeue(CurrentTask);
		CurrentTask.Result.bSuccess = CurrentTask.bSuccess = ProcessCurrentTask();

		if (bPaused)
		{
			return false;
		}

		CompletedTasks.Enqueue(CurrentTask);

		return CurrentTask.Result.bSuccess;
	}
};
