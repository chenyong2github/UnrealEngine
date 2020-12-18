// Copyright Epic Games, Inc. All Rights Reserved.

#include "WindowsRunnableThread.h"
#include "Containers/StringConv.h"
#include "CoreGlobals.h"
#include "GenericPlatform/GenericPlatformCrashContext.h"
#include "HAL/Event.h"
#include "HAL/ExceptionHandling.h"
#include "HAL/LowLevelMemTracker.h"
#include "HAL/ThreadManager.h"
#include "Misc/FeedbackContext.h"
#include "Misc/OutputDeviceError.h"
#include "Stats/Stats.h"

DEFINE_LOG_CATEGORY_STATIC(LogThreadingWindows, Log, All);

FRunnableThreadWin::~FRunnableThreadWin()
{
	if (Thread)
	{
		Kill(true);
	}
}

::DWORD STDCALL FRunnableThreadWin::_ThreadProc(LPVOID pThis)
{
	check(pThis);
	auto* ThisThread = (FRunnableThreadWin*)pThis;
	FThreadManager::Get().AddThread(ThisThread->GetThreadID(), ThisThread);
	return ThisThread->GuardedRun();
}

static int TranslateThreadPriority(EThreadPriority Priority)
{
	// If this triggers, 
	static_assert(TPri_Num == 7, "Need to add a case for new TPri_xxx enum value");

	switch (Priority)
	{
		case TPri_AboveNormal:			return THREAD_PRIORITY_ABOVE_NORMAL;
		case TPri_Normal:				return THREAD_PRIORITY_NORMAL;
		case TPri_BelowNormal:			return THREAD_PRIORITY_BELOW_NORMAL;
		case TPri_Highest:				return THREAD_PRIORITY_HIGHEST;
		case TPri_TimeCritical:			return THREAD_PRIORITY_HIGHEST;
		case TPri_Lowest:				return THREAD_PRIORITY_LOWEST;
		case TPri_SlightlyBelowNormal:	return THREAD_PRIORITY_BELOW_NORMAL;

	// Note: previously, the behaviour was:
	//
	//case TPri_AboveNormal:			return THREAD_PRIORITY_HIGHEST;
	//case TPri_Normal:					return THREAD_PRIORITY_HIGHEST - 1;
	//case TPri_BelowNormal:			return THREAD_PRIORITY_HIGHEST - 3;
	//case TPri_Highest:				return THREAD_PRIORITY_HIGHEST;
	//case TPri_TimeCritical:			return THREAD_PRIORITY_HIGHEST;
	//case TPri_Lowest:					return THREAD_PRIORITY_HIGHEST - 4;
	//case TPri_SlightlyBelowNormal:	return THREAD_PRIORITY_HIGHEST - 2;
	//
	// But the change (CL3747560) was not well documented (it didn't describe
	// the symptoms it was supposed to address) and introduces undesirable 
	// system behaviour on Windows since it starves out other processes in
	// the system when UE compiles shaders or otherwise goes wide due to the
	// inflation in priority (Normal mapped to THREAD_PRIORITY_ABOVE_NORMAL)
	// I kept the TPri_TimeCritical mapping to THREAD_PRIORITY_HIGHEST however
	// to avoid introducing poor behaviour since time critical priority is
	// similarly detrimental to overall system behaviour.
	//
	// If we discover thread scheduling issues it would maybe be better to 
	// adjust actual thread priorities at the source instead of this mapping?

	default: UE_LOG(LogHAL, Fatal, TEXT("Unknown Priority passed to TranslateThreadPriority()")); return THREAD_PRIORITY_NORMAL;
	}
}

void FRunnableThreadWin::SetThreadPriority(EThreadPriority NewPriority)
{
	ThreadPriority = NewPriority;

	::SetThreadPriority(Thread, TranslateThreadPriority(ThreadPriority));
}

void FRunnableThreadWin::Suspend(bool bShouldPause)
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

bool FRunnableThreadWin::Kill(bool bShouldWait)
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

void FRunnableThreadWin::WaitForCompletion()
{
	WaitForSingleObject(Thread, INFINITE);
}

bool FRunnableThreadWin::CreateInternal(
	FRunnable* InRunnable, 
	const TCHAR* InThreadName,
	uint32 InStackSize,
	EThreadPriority InThreadPri, 
	uint64 InThreadAffinityMask,
	EThreadCreateFlags InCreateFlags)
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

uint32 FRunnableThreadWin::GuardedRun()
{
	uint32 ExitCode = 0;

	FPlatformProcess::SetThreadAffinityMask(ThreadAffinityMask);

	FPlatformProcess::SetThreadName(*ThreadName);
	const TCHAR* CmdLine = ::GetCommandLineW();
	bool bNoExceptionHandler = FParse::Param(::GetCommandLineW(), TEXT("noexceptionhandler"));
#if UE_BUILD_DEBUG
	if (true && !GAlwaysReportCrash)
#else
	if (bNoExceptionHandler || (FPlatformMisc::IsDebuggerPresent() && !GAlwaysReportCrash))
#endif // UE_BUILD_DEBUG
	{
		ExitCode = Run();
	}
	else
	{
#if !PLATFORM_SEH_EXCEPTIONS_DISABLED
		__try
#endif // !PLATFORM_SEH_EXCEPTIONS_DISABLED
		{
			ExitCode = Run();
		}
#if !PLATFORM_SEH_EXCEPTIONS_DISABLED
		__except (ReportCrash( GetExceptionInformation() ))
		{
			__try
			{
				// Make sure the information which thread crashed makes it into the log.
				UE_LOG( LogThreadingWindows, Error, TEXT( "Runnable thread %s crashed." ), *ThreadName );
				GWarn->Flush();

				// Append the thread name at the end of the error report.
				FCString::Strncat( GErrorHist, LINE_TERMINATOR TEXT( "Crash in runnable thread " ), UE_ARRAY_COUNT( GErrorHist ) );
				FCString::Strncat( GErrorHist, *ThreadName, UE_ARRAY_COUNT( GErrorHist ) );

				// Crashed.
				ExitCode = 1;
				GError->HandleError();
				FPlatformMisc::RequestExit( true );
			}
			__except(EXCEPTION_EXECUTE_HANDLER)
			{
				// The crash handler crashed itself, exit with a code which the 
				// out-of-process monitor will be able to pick up and report into 
				// analytics.

				::exit(ECrashExitCodes::CrashHandlerCrashed);
			}
		}
#endif // !PLATFORM_SEH_EXCEPTIONS_DISABLED
	}

	return ExitCode;
}


uint32 FRunnableThreadWin::Run()
{
	uint32 ExitCode = 1;
	check(Runnable);

	if (Runnable->Init() == true)
	{
		ThreadInitSyncEvent->Trigger();

		// Setup TLS for this thread, used by FTlsAutoCleanup objects.
		SetTls();

		ExitCode = Runnable->Run();

		// Allow any allocated resources to be cleaned up
		Runnable->Exit();

#if STATS
		FThreadStats::Shutdown();
#endif
		FreeTls();
	}
	else
	{
		// Initialization has failed, release the sync event
		ThreadInitSyncEvent->Trigger();
	}

	return ExitCode;
}
