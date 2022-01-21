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

namespace UE {
    namespace PixelStreaming {
        /*
        * A thread with a single purpose, to poll GPU fences until they are done.
        * Polling jobs can be submitted to this thread using its static methods.
        * Once a polling job is complete a callback is called on the submitted job.
        */
        class FGPUFencePoller
        {
            public:
                FGPUFencePoller();
                void Shutdown();
                virtual ~FGPUFencePoller();
                void AddJob(FGPUFenceRHIRef Fence, TSharedRef<bool, ESPMode::ThreadSafe> bKeepPolling, TFunction<void()> FenceDoneCallback);
                static FGPUFencePoller* Get();

            private:
                struct FPollJob
                {
                    FPollJob();
                    FPollJob(FGPUFenceRHIRef InFence, TSharedRef<bool, ESPMode::ThreadSafe> bInKeepPolling, TFunction<void()> InFenceDoneCallback);
                    FGPUFenceRHIRef Fence;
                    TSharedRef<bool, ESPMode::ThreadSafe> bKeepPolling;
                    TFunction<void()> FenceDoneCallback;
                };

                class FPollerRunnable : public FRunnable
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
                    // End FRunnable interface
                private:
                    bool bIsRunning;
                    FEvent* JobAddedEvent;
                    TArray<FPollJob> PollJobs;
                    TQueue<FPollJob, EQueueMode::Mpsc> JobsToAdd;
                };

            private:
                FPollerRunnable Runnable;
                FRunnableThread* Thread;

                static FGPUFencePoller* Instance;
        };
    }
}
