// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/StringConv.h"
#include "CoreGlobals.h"
#include "CoreTypes.h"
#include "HAL/Event.h"
#include "HAL/LowLevelMemTracker.h"
#include "HAL/PlatformProcess.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/ThreadManager.h"
#include "Windows/WindowsHWrapper.h"

class FRunnable;

/**
 * This is the base interface for all runnable thread classes. It specifies the
 * methods used in managing its life cycle.
 */
class FRunnableThreadWin
	: public FRunnableThread
{
	/** The thread handle for the thread. */
	HANDLE Thread = 0;

	/**
	 * The thread entry point. Simply forwards the call on to the right
	 * thread main function
	 */
	static ::DWORD STDCALL _ThreadProc(LPVOID pThis)
	{
		check(pThis);
		auto* ThisThread = (FRunnableThreadWin*)pThis;
		FThreadManager::Get().AddThread(ThisThread->GetThreadID(), ThisThread);
		return ThisThread->GuardedRun();
	}

	/** Guarding works only if debugger is not attached or GAlwaysReportCrash is true. */
	uint32 GuardedRun();

	/**
	 * The real thread entry point. It calls the Init/Run/Exit methods on
	 * the runnable object
	 */
	uint32 Run();

public:
	~FRunnableThreadWin()
	{
		if (Thread)
		{
			Kill(true);
		}
	}

	virtual void SetThreadPriority(EThreadPriority NewPriority) override
	{
		ThreadPriority = NewPriority;

		::SetThreadPriority(Thread, TranslateThreadPriority(ThreadPriority));
	}

	virtual void Suspend(bool bShouldPause = true) override
	{
		check(Thread);
		if (bShouldPause == true)
		{
			SuspendThread(Thread);
		}
		else
		{
			ResumeThread(Thread);
		}
	}

	virtual bool Kill(bool bShouldWait = false) override
	{
		check(Thread && "Did you forget to call Create()?");
		bool bDidExitOK = true;

		// Let the runnable have a chance to stop without brute force killing
		if (Runnable)
		{
			Runnable->Stop();
		}

		if (bShouldWait == true)
		{
			// Wait indefinitely for the thread to finish.  IMPORTANT:  It's not safe to just go and
			// kill the thread with TerminateThread() as it could have a mutex lock that's shared
			// with a thread that's continuing to run, which would cause that other thread to
			// dead-lock.  
			//
			// This can manifest itself in code as simple as the synchronization
			// object that is used by our logging output classes

			WaitForSingleObject(Thread, INFINITE);
		}

		CloseHandle(Thread);
		Thread = NULL;

		return bDidExitOK;
	}

	virtual void WaitForCompletion() override
	{
		WaitForSingleObject(Thread, INFINITE);
	}

	virtual bool SetThreadAffinity(const FThreadAffinity& Affinity) override;

protected:
 	static int TranslateThreadPriority(EThreadPriority Priority);

	virtual bool CreateInternal(FRunnable* InRunnable, const TCHAR* InThreadName,
		uint32 InStackSize = 0,
		EThreadPriority InThreadPri = TPri_Normal, uint64 InThreadAffinityMask = 0,
		EThreadCreateFlags InCreateFlags = EThreadCreateFlags::None) override
	{
		static bool bOnce = false;
		if (!bOnce)
		{
			bOnce = true;
			::SetThreadPriority(::GetCurrentThread(), TranslateThreadPriority(TPri_Normal));
		}

		check(InRunnable);
		Runnable = InRunnable;
		ThreadAffinityMask = InThreadAffinityMask;

		// Create a sync event to guarantee the Init() function is called first
		ThreadInitSyncEvent = FPlatformProcess::GetSynchEventFromPool(true);

		ThreadName = InThreadName ? InThreadName : TEXT("Unnamed UE");
		ThreadPriority = InThreadPri;

		// Create the new thread
		{
			LLM_SCOPE(ELLMTag::ThreadStack);
			LLM_PLATFORM_SCOPE(ELLMTag::ThreadStackPlatform);
			// add in the thread size, since it's allocated in a black box we can't track
			// note: I don't see any accounting for this when threads are destroyed
			LLM(FLowLevelMemTracker::Get().OnLowLevelAlloc(ELLMTracker::Default, nullptr, InStackSize));
			LLM(FLowLevelMemTracker::Get().OnLowLevelAlloc(ELLMTracker::Platform, nullptr, InStackSize));

			// Create the thread as suspended, so we can ensure ThreadId is initialized and the thread manager knows about the thread before it runs.
			Thread = CreateThread(NULL, InStackSize, _ThreadProc, this, STACK_SIZE_PARAM_IS_A_RESERVATION | CREATE_SUSPENDED, (::DWORD*)&ThreadID);
		}

		// If it fails, clear all the vars
		if (Thread == NULL)
		{
			Runnable = nullptr;
		}
		else
		{
			ResumeThread(Thread);

			// Let the thread start up
			ThreadInitSyncEvent->Wait(INFINITE);

			ThreadPriority = TPri_Normal; // Set back to default in case any SetThreadPrio() impls compare against current value to reduce syscalls
			SetThreadPriority(InThreadPri);
		}

		// Cleanup the sync event
		FPlatformProcess::ReturnSynchEventToPool(ThreadInitSyncEvent);
		ThreadInitSyncEvent = nullptr;
		return Thread != NULL;
	}
};
