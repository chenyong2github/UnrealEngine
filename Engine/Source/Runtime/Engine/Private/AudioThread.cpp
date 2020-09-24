// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AudioThread.cpp: Audio thread implementation.
=============================================================================*/

#include "AudioThread.h"
#include "HAL/PlatformProcess.h"
#include "HAL/RunnableThread.h"
#include "HAL/ExceptionHandling.h"
#include "Misc/CoreStats.h"
#include "UObject/UObjectGlobals.h"
#include "Audio.h"
#include "HAL/LowLevelMemTracker.h"

//
// Globals
//

int32 GCVarSuspendAudioThread = 0;
TAutoConsoleVariable<int32> CVarSuspendAudioThread(TEXT("AudioThread.SuspendAudioThread"), GCVarSuspendAudioThread, TEXT("0=Resume, 1=Suspend"), ECVF_Cheat);

int32 GCVarAboveNormalAudioThreadPri = 0;
TAutoConsoleVariable<int32> CVarAboveNormalAudioThreadPri(TEXT("AudioThread.AboveNormalPriority"), GCVarAboveNormalAudioThreadPri, TEXT("0=Normal, 1=AboveNormal"), ECVF_Default);

int32 GCVarEnableAudioCommandLogging = 0;
TAutoConsoleVariable<int32> CVarEnableAudioCommandLogging(TEXT("AudioThread.EnableAudioCommandLogging"), GCVarEnableAudioCommandLogging, TEXT("0=Disbaled, 1=Enabled"), ECVF_Default);

static int32 GCVarEnableBatchProcessing = 1;
TAutoConsoleVariable<int32> CVarEnableBatchProcessing(
	TEXT("AudioThread.EnableBatchProcessing"),
	GCVarEnableBatchProcessing,
	TEXT("Enables batch processing audio thread commands.\n")
	TEXT("0: Not Enabled, 1: Enabled"),
	ECVF_Default); 

static int32 GBatchAudioAsyncBatchSize = 128;
static FAutoConsoleVariableRef CVarBatchAudioAsyncBatchSize(
	TEXT("AudioThread.BatchAsyncBatchSize"),
	GBatchAudioAsyncBatchSize,
	TEXT("When AudioThread.EnableBatchProcessing = 1, controls the number of audio commands grouped together for threading.")
);


static int32 GAudioCommandFenceWaitTimeMs = 35;
TAutoConsoleVariable<int32> CVarAudioCommandFenceWaitTimeMs(
	TEXT("AudioCommand.FenceWaitTimeMs"),
	GAudioCommandFenceWaitTimeMs, 
	TEXT("Sets number of ms for fence wait"), 
	ECVF_Default);

struct FAudioThreadInteractor
{
	static void UseAudioThreadCVarSinkFunction()
	{
		static bool bLastSuspendAudioThread = false;
		const bool bSuspendAudioThread = (CVarSuspendAudioThread.GetValueOnGameThread() != 0);

		if (bLastSuspendAudioThread != bSuspendAudioThread)
		{
			bLastSuspendAudioThread = bSuspendAudioThread;
			if (bSuspendAudioThread && IsAudioThreadRunning())
			{
				FAudioThread::SuspendAudioThread();
			}
			else if (GIsAudioThreadSuspended)
			{
				FAudioThread::ResumeAudioThread();
			}
			else if (GIsEditor)
			{
				UE_LOG(LogAudio, Warning, TEXT("Audio threading is disabled in the editor."));
			}
			else if (!FAudioThread::IsUsingThreadedAudio())
			{
				UE_LOG(LogAudio, Warning, TEXT("Cannot manipulate audio thread when disabled by platform or ini."));
			}
		}
	}
};

static FAutoConsoleVariableSink CVarUseAudioThreadSink(FConsoleCommandDelegate::CreateStatic(&FAudioThreadInteractor::UseAudioThreadCVarSinkFunction));

TAtomic<bool> FAudioThread::bIsAudioThreadRunning(false);
bool FAudioThread::bUseThreadedAudio = false;
FRunnable* FAudioThread::AudioThreadRunnable = nullptr;
FCriticalSection FAudioThread::CurrentAudioThreadStatIdCS;
TStatId FAudioThread::CurrentAudioThreadStatId;
TStatId FAudioThread::LongestAudioThreadStatId;
double FAudioThread::LongestAudioThreadTimeMsec = 0.0;

/** The audio thread main loop */
void AudioThreadMain( FEvent* TaskGraphBoundSyncEvent )
{
	FTaskGraphInterface::Get().AttachToThread(ENamedThreads::AudioThread);
	FPlatformMisc::MemoryBarrier();

	// Inform main thread that the audio thread has been attached to the taskgraph and is ready to receive tasks
	if( TaskGraphBoundSyncEvent != nullptr )
	{
		TaskGraphBoundSyncEvent->Trigger();
	}

	FTaskGraphInterface::Get().ProcessThreadUntilRequestReturn(ENamedThreads::AudioThread);
	FPlatformMisc::MemoryBarrier();
}

FAudioThread::FAudioThread()
{
	TaskGraphBoundSyncEvent	= FPlatformProcess::GetSynchEventFromPool(true);

	FCoreUObjectDelegates::GetPreGarbageCollectDelegate().AddRaw(this, &FAudioThread::OnPreGarbageCollect);
	FCoreUObjectDelegates::GetPostGarbageCollect().AddRaw(this, &FAudioThread::OnPostGarbageCollect);

	FCoreUObjectDelegates::PreGarbageCollectConditionalBeginDestroy.AddRaw(this, &FAudioThread::OnPreGarbageCollect);
	FCoreUObjectDelegates::PostGarbageCollectConditionalBeginDestroy.AddRaw(this, &FAudioThread::OnPostGarbageCollect);
}

FAudioThread::~FAudioThread()
{
	FCoreUObjectDelegates::GetPreGarbageCollectDelegate().RemoveAll(this);
	FCoreUObjectDelegates::GetPostGarbageCollect().RemoveAll(this);

	FCoreUObjectDelegates::PreGarbageCollectConditionalBeginDestroy.RemoveAll(this);
	FCoreUObjectDelegates::PostGarbageCollectConditionalBeginDestroy.RemoveAll(this);

	FPlatformProcess::ReturnSynchEventToPool(TaskGraphBoundSyncEvent);
	TaskGraphBoundSyncEvent = nullptr;
}

static int32 AudioThreadSuspendCount = 0;

void FAudioThread::SuspendAudioThread()
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	check(FPlatformTLS::GetCurrentThreadId() == GGameThreadId);
	check(!GIsAudioThreadSuspended.Load() || CVarSuspendAudioThread.GetValueOnGameThread() != 0);
	if (bIsAudioThreadRunning.Load())
	{
		// Make GC wait on the audio thread finishing processing
		FAudioCommandFence AudioFence;
		AudioFence.BeginFence();
		AudioFence.Wait();

		GIsAudioThreadSuspended = true;
		FPlatformMisc::MemoryBarrier();
		bIsAudioThreadRunning = false;
	}
	check(!bIsAudioThreadRunning.Load());

PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void FAudioThread::ResumeAudioThread()
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS

	check(FPlatformTLS::GetCurrentThreadId() == GGameThreadId);
	if (GIsAudioThreadSuspended.Load() && CVarSuspendAudioThread.GetValueOnGameThread() == 0)
	{
		GIsAudioThreadSuspended = false;
		FPlatformMisc::MemoryBarrier();
		bIsAudioThreadRunning = true;
	}
	ProcessAllCommands();

PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void FAudioThread::OnPreGarbageCollect()
{
	AudioThreadSuspendCount++;
	if (AudioThreadSuspendCount == 1)
	{
		SuspendAudioThread();
	}
}

void FAudioThread::OnPostGarbageCollect()
{
	AudioThreadSuspendCount--;
	if (AudioThreadSuspendCount == 0)
	{
		ResumeAudioThread();
	}
}

bool FAudioThread::Init()
{ 
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	GAudioThreadId = FPlatformTLS::GetCurrentThreadId();
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	return true;
}

void FAudioThread::Exit()
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	GAudioThreadId = 0;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	FPlatformProcess::TeardownAudioThread();
}

uint32 FAudioThread::Run()
{
	LLM_SCOPE(ELLMTag::AudioMisc);

	FMemory::SetupTLSCachesOnCurrentThread();
	FPlatformProcess::SetupAudioThread();
	AudioThreadMain( TaskGraphBoundSyncEvent );
	FMemory::ClearAndDisableTLSCachesOnCurrentThread();
	return 0;
}

void FAudioThread::SetUseThreadedAudio(const bool bInUseThreadedAudio)
{
	if (bIsAudioThreadRunning.Load() && !bInUseThreadedAudio)
	{
		UE_LOG(LogAudio, Error, TEXT("You cannot disable using threaded audio once the thread has already begun running."));
	}
	else
	{
		bUseThreadedAudio = bInUseThreadedAudio;
	}
}

bool FAudioThread::IsUsingThreadedAudio()
{
	return bUseThreadedAudio;
}

bool FAudioThread::IsAudioThreadRunning()
{ 
	return bIsAudioThreadRunning; 
}

struct FAudioAsyncBatcher
{
	FGraphEventArray DispatchEvent;
	int32 NumBatched = 0;


	FGraphEventArray* GetAsyncPrereq()
	{
		check(IsInGameThread());
#if !WITH_EDITOR
		if (GCVarEnableBatchProcessing)
		{
			if (NumBatched >= GBatchAudioAsyncBatchSize || !DispatchEvent.Num() || !DispatchEvent[0].GetReference() || DispatchEvent[0]->IsComplete())
			{
				Flush();
			}
			if (DispatchEvent.Num() == 0)
			{
				check(NumBatched == 0);
				DispatchEvent.Add(FGraphEvent::CreateGraphEvent());
			}
			NumBatched++;
			return &DispatchEvent;
		}
#endif
		return nullptr;
	}

	void Flush()
	{
		check(IsInGameThread());
		if (NumBatched)
		{
			check(DispatchEvent.Num() && DispatchEvent[0].GetReference() && !DispatchEvent[0]->IsComplete());
			FGraphEventRef Dispatch = DispatchEvent[0];
			TFunction<void()> FlushAudioCommands = [Dispatch]()
			{
				TArray<FBaseGraphTask*> NewTasks;
				Dispatch->DispatchSubsequents(NewTasks);
			};

			FFunctionGraphTask::CreateAndDispatchWhenReady(MoveTemp(FlushAudioCommands), TStatId(), nullptr, ENamedThreads::AudioThread);

			DispatchEvent.Empty();
			NumBatched = 0;
		}
	}

};

static FAudioAsyncBatcher GAudioAsyncBatcher;

void FAudioThread::RunCommandOnAudioThread(TFunction<void()> InFunction, const TStatId InStatId)
{
	check(FPlatformTLS::GetCurrentThreadId() == GGameThreadId);
	if (bIsAudioThreadRunning)
	{
		if (GCVarEnableAudioCommandLogging == 1)
		{
			TFunction<void()> FuncWrapper = [InFunction, InStatId]()
			{
				FAudioThread::SetCurrentAudioThreadStatId(InStatId);

				// Time the execution of the function
				const double StartTime = FPlatformTime::Seconds();

				// Execute the function
				InFunction();

				// Track the longest one
				const double DeltaTime = (FPlatformTime::Seconds() - StartTime) * 1000.0f;
				if (DeltaTime > GetCurrentLongestTime())
				{
					SetLongestTimeAndId(InStatId, DeltaTime);
				}
			};

			FFunctionGraphTask::CreateAndDispatchWhenReady(MoveTemp(FuncWrapper), InStatId, GAudioAsyncBatcher.GetAsyncPrereq(), ENamedThreads::AudioThread);
		}
		else
		{
			FFunctionGraphTask::CreateAndDispatchWhenReady(MoveTemp(InFunction), InStatId, GAudioAsyncBatcher.GetAsyncPrereq(), ENamedThreads::AudioThread);
		}
	}
	else
	{
		FScopeCycleCounter ScopeCycleCounter(InStatId);
		InFunction();
	}
}

void FAudioThread::SetCurrentAudioThreadStatId(TStatId InStatId)
{
	FScopeLock Lock(&CurrentAudioThreadStatIdCS);
	CurrentAudioThreadStatId = InStatId;
}

FString FAudioThread::GetCurrentAudioThreadStatId()
{
	FScopeLock Lock(&CurrentAudioThreadStatIdCS);
#if STATS
	return FString(CurrentAudioThreadStatId.GetStatDescriptionANSI());
#else
	return FString(TEXT("NoStats"));
#endif
}

void FAudioThread::ResetAudioThreadTimers()
{
	FScopeLock Lock(&CurrentAudioThreadStatIdCS);
	LongestAudioThreadStatId = TStatId();
	LongestAudioThreadTimeMsec = 0.0;
}

void FAudioThread::SetLongestTimeAndId(TStatId NewLongestId, double LongestTimeMsec)
{
	FScopeLock Lock(&CurrentAudioThreadStatIdCS);
	LongestAudioThreadTimeMsec = LongestTimeMsec;
	LongestAudioThreadStatId = NewLongestId;
}

void FAudioThread::GetLongestTaskInfo(FString& OutLongestTask, double& OutLongestTaskTimeMs)
{
	FScopeLock Lock(&CurrentAudioThreadStatIdCS);
#if STATS
	OutLongestTask = FString(LongestAudioThreadStatId.GetStatDescriptionANSI());
#else
	OutLongestTask = FString(TEXT("NoStats"));
#endif
	OutLongestTaskTimeMs = LongestAudioThreadTimeMsec;
}

void FAudioThread::ProcessAllCommands()
{
	if (bIsAudioThreadRunning)
	{
		GAudioAsyncBatcher.Flush();
	}
	else
	{
		check(!GAudioAsyncBatcher.NumBatched);
	}
}

void FAudioThread::RunCommandOnGameThread(TFunction<void()> InFunction, const TStatId InStatId)
{
	if (bIsAudioThreadRunning)
	{
		check(IsInAudioThread());
		FFunctionGraphTask::CreateAndDispatchWhenReady(MoveTemp(InFunction), InStatId, nullptr, ENamedThreads::GameThread);
	}
	else
	{
		check(FPlatformTLS::GetCurrentThreadId() == GGameThreadId);
		FScopeCycleCounter ScopeCycleCounter(InStatId);
		InFunction();
	}
}

void FAudioThread::StartAudioThread()
{
	check(FPlatformTLS::GetCurrentThreadId() == GGameThreadId);

	check(!bIsAudioThreadRunning.Load());
	check(!GIsAudioThreadSuspended.Load());
	if (bUseThreadedAudio)
	{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		check(GAudioThread == nullptr);
PRAGMA_ENABLE_DEPRECATION_WARNINGS

		static uint32 ThreadCount = 0;
		check(!ThreadCount); // we should not stop and restart the audio thread; it is complexity we don't need.

		bIsAudioThreadRunning = true;

		// Create the audio thread.
		AudioThreadRunnable = new FAudioThread();

PRAGMA_DISABLE_DEPRECATION_WARNINGS
		GAudioThread = 
PRAGMA_ENABLE_DEPRECATION_WARNINGS
			FRunnableThread::Create(AudioThreadRunnable, *FName(NAME_AudioThread).GetPlainNameString(), 0, (CVarAboveNormalAudioThreadPri.GetValueOnGameThread() == 0) ? TPri_BelowNormal : TPri_AboveNormal, FPlatformAffinity::GetAudioThreadMask());

		// Wait for audio thread to have taskgraph bound before we dispatch any tasks for it.
		((FAudioThread*)AudioThreadRunnable)->TaskGraphBoundSyncEvent->Wait();

		// ensure the thread has actually started and is idling
		FAudioCommandFence Fence;
		Fence.BeginFence();
		Fence.Wait();

		ThreadCount++;

		if (CVarSuspendAudioThread.GetValueOnGameThread() != 0)
		{
			SuspendAudioThread();
		}
	}
}

void FAudioThread::StopAudioThread()
{
	check(FPlatformTLS::GetCurrentThreadId() == GGameThreadId);
	check(!GIsAudioThreadSuspended.Load() || CVarSuspendAudioThread.GetValueOnGameThread() != 0);

	if (!bIsAudioThreadRunning.Load())
	{
		return;
	}

	FAudioCommandFence Fence;
	Fence.BeginFence();
	Fence.Wait();
	FGraphEventRef QuitTask = TGraphTask<FReturnGraphTask>::CreateTask(nullptr, ENamedThreads::GameThread).ConstructAndDispatchWhenReady(ENamedThreads::AudioThread);

	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_StopAudioThread);
		FTaskGraphInterface::Get().WaitUntilTaskCompletes(QuitTask, ENamedThreads::GameThread_Local);
	}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	// Wait for the audio thread to return.
	GAudioThread->WaitForCompletion();

	// Destroy the audio thread objects.
	delete GAudioThread;
	GAudioThread = nullptr;
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	bIsAudioThreadRunning = false;

	delete AudioThreadRunnable;
	AudioThreadRunnable = nullptr;
}

FAudioCommandFence::FAudioCommandFence()
	: FenceDoneEvent(nullptr)
{
}

FAudioCommandFence::~FAudioCommandFence()
{
	if (FenceDoneEvent)
	{
		FenceDoneEvent->Wait();

		FPlatformProcess::ReturnSynchEventToPool(FenceDoneEvent);
		FenceDoneEvent = nullptr;
	}
}

void FAudioCommandFence::BeginFence()
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	if (FAudioThread::IsAudioThreadRunning())
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	{
		DECLARE_CYCLE_STAT(TEXT("FNullGraphTask.FenceAudioCommand"),
			STAT_FNullGraphTask_FenceAudioCommand,
			STATGROUP_TaskGraphTasks);

		CompletionEvent = TGraphTask<FNullGraphTask>::CreateTask(GAudioAsyncBatcher.GetAsyncPrereq(), ENamedThreads::GameThread).ConstructAndDispatchWhenReady(
			GET_STATID(STAT_FNullGraphTask_FenceAudioCommand), ENamedThreads::AudioThread);

		if (FenceDoneEvent)
		{
			FenceDoneEvent->Wait();

			FPlatformProcess::ReturnSynchEventToPool(FenceDoneEvent);
			FenceDoneEvent = nullptr;
		}

		FenceDoneEvent = FPlatformProcess::GetSynchEventFromPool(true);

		FTaskGraphInterface::Get().TriggerEventWhenTaskCompletes(FenceDoneEvent, CompletionEvent, ENamedThreads::GameThread, ENamedThreads::AudioThread);

		FAudioThread::ProcessAllCommands();
	}
	else
	{
		CompletionEvent = nullptr;
	}
}

bool FAudioCommandFence::IsFenceComplete() const
{
	FAudioThread::ProcessAllCommands();
	
	if (!CompletionEvent.GetReference() || CompletionEvent->IsComplete())
	{
		CompletionEvent = nullptr; // this frees the handle for other uses, the NULL state is considered completed
		return true;
	}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	check(FAudioThread::IsAudioThreadRunning());
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	return FenceDoneEvent->Wait(0);
}

/**
 * Waits for pending fence commands to retire.
 */
void FAudioCommandFence::Wait(bool bProcessGameThreadTasks) const
{
	FAudioThread::ProcessAllCommands();

	if (!IsFenceComplete()) // this checks the current thread
	{
		const double StartTime = FPlatformTime::Seconds();
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FAudioCommandFence_Wait);

		bool bDone = false;

		do
		{
			if (FenceDoneEvent)
			{
				bDone = FenceDoneEvent->Wait(GAudioCommandFenceWaitTimeMs);
			}
			else
			{
				bDone = true;
			}
			
			if(bDone && FenceDoneEvent)
			{
				FPlatformProcess::ReturnSynchEventToPool(FenceDoneEvent);
				FenceDoneEvent = nullptr;
			}

			// Log how long we've been waiting for the audio thread:
			float ThisTime = FPlatformTime::Seconds() - StartTime;
 			if (ThisTime > static_cast<float>(GAudioCommandFenceWaitTimeMs) / 1000.0f + SMALL_NUMBER)
			{
				if (GCVarEnableAudioCommandLogging == 1)
				{
					FString CurrentTask = FAudioThread::GetCurrentAudioThreadStatId();

					FString LongestTask;
					double LongestTaskTimeMs;
					FAudioThread::GetLongestTaskInfo(LongestTask, LongestTaskTimeMs);

					UE_LOG(LogAudio, Display, TEXT("Waited %.2f ms for audio thread. (Current Task: %s, Longest task: %s %.2f ms)"), ThisTime * 1000.0f, *CurrentTask, *LongestTask, LongestTaskTimeMs);
				}
				else
				{
					UE_LOG(LogAudio, Display,  TEXT("Waited %f ms for audio thread."), ThisTime * 1000.0f);
				}
			}
		} while (!bDone);

		FAudioThread::ResetAudioThreadTimers();
	}
}

