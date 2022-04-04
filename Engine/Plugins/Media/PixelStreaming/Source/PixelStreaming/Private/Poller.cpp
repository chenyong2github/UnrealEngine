// Copyright Epic Games, Inc. All Rights Reserved.
#include "Poller.h"

namespace UE::PixelStreaming
{
	/*
	* ------------- UE::PixelStreaming::FPoller ---------------------
	*/
	FPoller::FPoller()
		: Runnable()
		, Thread(FRunnableThread::Create(&Runnable, TEXT("Pixel Streaming Poller")))
	{
	}

	FPoller::~FPoller()
	{
		Shutdown();
		Thread->Kill(true);
	}

	void FPoller::Shutdown()
	{
		Runnable.Stop();
	}

	void FPoller::AddJob(TFunction<bool()> InIsJobDone, TSharedRef<bool, ESPMode::ThreadSafe> bKeepPolling, TFunction<void()> InJobDoneCallback)
	{
		checkf(Runnable.IsRunning(), TEXT("Poller runnable must be running to submit jobs to it."));
		Runnable.AddJob({ InIsJobDone, bKeepPolling, InJobDoneCallback });
	}

	/*
	* ------------------- FPollJob ----------------------
	*/
	FPoller::FPollJob::FPollJob(TFunction<bool()> InIsJobDone, TSharedRef<bool, ESPMode::ThreadSafe> bInKeepPolling, TFunction<void()> InJobDoneCallback)
		: IsJobDone(InIsJobDone)
		, bKeepPolling(bInKeepPolling)
		, JobDoneCallback(InJobDoneCallback)
	{
	}

	FPoller::FPollJob::FPollJob()
		: FPollJob([]()->bool { return true; }, MakeShared<bool, ESPMode::ThreadSafe>(false), []() {})
	{
	}

	/*
	* ------------- FPollerRunnable ---------------------
	*/
	FPoller::FPollerRunnable::FPollerRunnable()
		: bIsRunning(false)
		, JobAddedEvent(FPlatformProcess::GetSynchEventFromPool(false))
	{
	}

	bool FPoller::FPollerRunnable::IsRunning() const
	{
		return bIsRunning;
	}

	bool FPoller::FPollerRunnable::Init()
	{
		return true;
	}

	void FPoller::FPollerRunnable::AddJob(FPollJob Job)
	{
		// Note: this is threadsafe because use of TQueue
		JobsToAdd.Enqueue(Job);
		JobAddedEvent->Trigger();
	}

	uint32 FPoller::FPollerRunnable::Run()
	{
		bIsRunning = true;

		while (bIsRunning)
		{
			Poll(false);	
		}

		return 0;
	}

	void FPoller::FPollerRunnable::Tick()
	{
		Poll(true);
	}

	void FPoller::FPollerRunnable::Poll(bool bIsRunningSingleThreaded)
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

			bool bJobCompleted = Job.IsJobDone();
			if (bJobCompleted)
			{
				Job.JobDoneCallback();
				PollJobs.RemoveAt(i);
			}
		}

		if(!bIsRunningSingleThreaded)
		{
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
	}

	void FPoller::FPollerRunnable::Stop()
	{
		bIsRunning = false;
		JobAddedEvent->Trigger();
	}

	void FPoller::FPollerRunnable::Exit()
	{
		bIsRunning = false;
		JobAddedEvent->Trigger();
	}
} // namespace UE::PixelStreaming