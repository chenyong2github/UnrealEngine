// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/Queue.h"
#include "Containers/Array.h"
#include "RHI.h"
#include "RHIResources.h"
#include "Templates/SharedPointer.h"
#include "HAL/Event.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "HAL/RunnableThread.h"
#include "HAL/Runnable.h"
#include "Misc/SingleThreadRunnable.h"

namespace UE::PixelStreaming
{
	/*
	* A thread with a single purpose, to poll GPU fences until they are done.
	* Polling jobs can be submitted to this thread using its static methods.
	* Once a polling job is complete a callback is called on the submitted job.
	*/
	class PIXELSTREAMING_API FPoller
	{
	public:
		FPoller();
		void Shutdown();
		virtual ~FPoller();
		void AddJob(TFunction<bool()> InIsJobDone, TSharedRef<bool, ESPMode::ThreadSafe> bKeepPolling, TFunction<void()> InJobDoneCallback);

	private:
		struct FPollJob
		{
			FPollJob();
			FPollJob(TFunction<bool()> InIsJobDone, TSharedRef<bool, ESPMode::ThreadSafe> bInKeepPolling, TFunction<void()> InJobDoneCallback);

			TFunction<bool()> IsJobDone;
			TSharedRef<bool, ESPMode::ThreadSafe> bKeepPolling;
			TFunction<void()> JobDoneCallback;
		};

		class FPollerRunnable : public FRunnable, public FSingleThreadRunnable
		{
		public:
			FPollerRunnable();
			void AddJob(FPollJob Job);
			bool IsRunning() const;
			// Begin FRunnable interface.
			virtual bool Init() override;
			virtual uint32 Run() override;
			virtual void Stop() override;
			virtual void Exit() override;
			virtual FSingleThreadRunnable* GetSingleThreadInterface() override { bIsRunning = true; return this; };
			// End FRunnable interface

			// Begin FSingleThreadRunnable interface.
			virtual void Tick() override;
			// End FSingleThreadRunnable interface
		private:
			void Poll(bool bIsRunningSingleThreaded);
			bool bIsRunning;
			FEvent* JobAddedEvent;
			TArray<FPollJob> PollJobs;
			TQueue<FPollJob, EQueueMode::Mpsc> JobsToAdd;
		};

	private:
		FPollerRunnable Runnable;
		FRunnableThread* Thread;
	};
} // namespace UE::PixelStreaming
