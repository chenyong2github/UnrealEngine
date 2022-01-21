// Copyright Epic Games, Inc. All Rights Reserved.
#include "GPUFencePoller.h"

/*
* ------------- UE::PixelStreaming::FGPUFencePoller ---------------------
*/

UE::PixelStreaming::FGPUFencePoller* UE::PixelStreaming::FGPUFencePoller::Instance = nullptr;

UE::PixelStreaming::FGPUFencePoller* UE::PixelStreaming::FGPUFencePoller::Get()
{
	checkf(Instance, TEXT("You should not try to Get() and instance of the poller before it has been constructed somewhere yet."));
	return UE::PixelStreaming::FGPUFencePoller::Instance;
}

UE::PixelStreaming::FGPUFencePoller::FGPUFencePoller()
	: Runnable()
	, Thread(FRunnableThread::Create(&Runnable, TEXT("Pixel Streaming GPUFencePoller")))
{
	UE::PixelStreaming::FGPUFencePoller::Instance = this;
}

UE::PixelStreaming::FGPUFencePoller::~FGPUFencePoller()
{
	Shutdown();
	Thread->Kill(true);
}

void UE::PixelStreaming::FGPUFencePoller::Shutdown()
{
	Runnable.Stop();
}

void UE::PixelStreaming::FGPUFencePoller::AddJob(FGPUFenceRHIRef Fence, TSharedRef<bool, ESPMode::ThreadSafe> bKeepPolling, TFunction<void()> FenceDoneCallback)
{
	checkf(Runnable.IsRunning(), TEXT("Poller runnable must be running to submit jobs to it."));
	Runnable.AddJob({ Fence, bKeepPolling, FenceDoneCallback });
}

/*
* ------------------- FPollJob ----------------------
*/

UE::PixelStreaming::FGPUFencePoller::FPollJob::FPollJob(FGPUFenceRHIRef InFence, TSharedRef<bool, ESPMode::ThreadSafe> bInKeepPolling, TFunction<void()> InFenceDoneCallback)
	: Fence(InFence)
	, bKeepPolling(bInKeepPolling)
	, FenceDoneCallback(InFenceDoneCallback)
{
	
}

UE::PixelStreaming::FGPUFencePoller::FPollJob::FPollJob()
	: FPollJob(FGPUFenceRHIRef(), MakeShared<bool, ESPMode::ThreadSafe>(false), []() {})
{
}

/*
* ------------- FPollerRunnable ---------------------
*/

UE::PixelStreaming::FGPUFencePoller::FPollerRunnable::FPollerRunnable()
	: bIsRunning(false)
	, JobAddedEvent(FPlatformProcess::GetSynchEventFromPool(false))
{
}

bool UE::PixelStreaming::FGPUFencePoller::FPollerRunnable::IsRunning() const
{
	return bIsRunning;
}

bool UE::PixelStreaming::FGPUFencePoller::FPollerRunnable::Init()
{
	return true;
}

void UE::PixelStreaming::FGPUFencePoller::FPollerRunnable::AddJob(FPollJob Job)
{
	// Note: this is threadsafe because use of TQueue
	JobsToAdd.Enqueue(Job);
	JobAddedEvent->Trigger();
}

uint32 UE::PixelStreaming::FGPUFencePoller::FPollerRunnable::Run()
{
	bIsRunning = true;

	while (bIsRunning)
	{

		// Add any new jobs
		while (!JobsToAdd.IsEmpty())
		{
			FPollJob* Job = JobsToAdd.Peek();
			if (Job)
			{
				PollJobs.Add(*Job);
			}
			JobsToAdd.Pop();
		}

		// Iterate backwards so we can remove elements if the job is completed
		for (int i = PollJobs.Num() - 1; i >= 0; i--)
		{
			FPollJob& Job = PollJobs[i];
			if (Job.bKeepPolling.Get() == false)
			{
				PollJobs.RemoveAt(i);
				continue;
			}

			bool bJobCompleted = Job.Fence->Poll();
			if (bJobCompleted)
			{
				Job.FenceDoneCallback();
				PollJobs.RemoveAt(i);
			}
		}

		//No jobs so sleep this thread waiting for a job to be added.
		if (PollJobs.Num() == 0)
		{
			JobAddedEvent->Wait(); 
		}
		else
		{
			// Free CPU usage up for other threads.
			FPlatformProcess::Sleep(0.0f);
		}
	}

	return 0;
}

void UE::PixelStreaming::FGPUFencePoller::FPollerRunnable::Stop()
{
	bIsRunning = false;
	JobAddedEvent->Trigger();
}

void UE::PixelStreaming::FGPUFencePoller::FPollerRunnable::Exit()
{
	bIsRunning = false;
	JobAddedEvent->Trigger();
}
