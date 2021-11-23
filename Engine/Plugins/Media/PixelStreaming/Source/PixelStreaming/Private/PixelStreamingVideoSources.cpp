// Copyright Epic Games, Inc. All Rights Reserved.
#include "PixelStreamingVideoSources.h"
#include "CoreGlobals.h"
#include "Misc/ScopedEvent.h"
#include "VulkanRHIPrivate.h"
#include "PixelStreamingSettings.h"
#include "Async/Async.h"

FPixelStreamingVideoSources::FPixelStreamingVideoSources()
    : bMatchFrameBufferResolution(true)
    , bFixedResolution(PixelStreamingSettings::CVarPixelStreamingWebRTCDisableResolutionChange.GetValueOnAnyThread())
    , ThreadWaiter(FPlatformProcess::GetSynchEventFromPool(false))
    , bIsThreadRunning(true)
{
	this->FrameDelayMs = (1.0f / PixelStreamingSettings::CVarPixelStreamingWebRTCMaxFps.GetValueOnAnyThread()) * 1000.0f;

    this->VideoSourcesThread = MakeUnique<FThread>(TEXT("VideoSourcesThread"), [this]() { this->RunVideoSourcesLoop_VideoSourcesThread(); });
}

FPixelStreamingVideoSources::FPixelStreamingVideoSources(int Width, int Height)
    : StartResolution(FIntPoint(Width, Height))
    , bMatchFrameBufferResolution(false)
    , bFixedResolution(true)
    , ThreadWaiter(FPlatformProcess::GetSynchEventFromPool(false))
    , bIsThreadRunning(true)
{
    this->VideoSourcesThread = MakeUnique<FThread>(TEXT("VideoSourcesThread"), [this]() { this->RunVideoSourcesLoop_VideoSourcesThread(); });
}

FPixelStreamingVideoSources::~FPixelStreamingVideoSources()
{
    this->bIsThreadRunning = false;
    this->ThreadWaiter->Trigger();
    this->VideoSourcesThread->Join();
}

void FPixelStreamingVideoSources::QueueTask(TFunction<void()> InTaskToQueue)
{
    this->QueuedTasks.Enqueue(InTaskToQueue);
    // This will wake the VideoSourcesThread if it is sleeping.
    this->ThreadWaiter->Trigger();
}

webrtc::VideoTrackSourceInterface* FPixelStreamingVideoSources::CreateVideoSource(FPlayerId PlayerId)
{
    if(IsInVideoSourcesThread())
    {
        return this->CreateVideoSource_VideoSourcesThread(PlayerId);
    }
    else
    {
        webrtc::VideoTrackSourceInterface* VideoSource = nullptr;

        // Start scoped thread blocker here
        {
            FScopedEvent ThreadBlocker;

            this->QueueTask([this, PlayerId, &VideoSource, &ThreadBlocker]()
            {
                VideoSource = this->CreateVideoSource_VideoSourcesThread(PlayerId);

                // Unblocks the thread
                ThreadBlocker.Trigger();
            });

            // Blocks current thread until video sources thread calls trigger (indicating it is done)
        }

        return VideoSource;
    }
}

void FPixelStreamingVideoSources::SetQualityController(FPlayerId PlayerId)
{
    this->QueueTask([this, PlayerId](){ this->SetQualityController_VideoSourcesThread(PlayerId); });
}

void FPixelStreamingVideoSources::DeleteVideoSource(FPlayerId PlayerId)
{
    this->QueueTask([this, PlayerId](){ this->DeleteVideoSource_VideoSourcesThread(PlayerId); });
}

void FPixelStreamingVideoSources::OnFrameReady(const FTexture2DRHIRef& FrameBuffer)
{
    checkf(IsInRenderingThread(), TEXT("This method must be called on the rendering thread."));

    // If we have not created video capturer yet, do it here.
    if(!this->VideoCapturerContext.IsValid())
    {
        // Make FVideoEncoderInput at the appropriate resolution based on VideoSource settings
        this->StartResolution = this->bMatchFrameBufferResolution ? FrameBuffer->GetSizeXY() : this->StartResolution;
        this->VideoCapturerContext = MakeShared<FVideoCapturerContext, ESPMode::ThreadSafe>(this->StartResolution.X, this->StartResolution.Y, this->bFixedResolution);
    }

    // Copy frame.
    this->VideoCapturerContext->CaptureFrame(FrameBuffer);

    // Backbuffer now has something captured, thread can wake up (if needed).
    this->ThreadWaiter->Trigger();
}

bool FPixelStreamingVideoSources::IsInVideoSourcesThread()
{
    if(this->VideoSourcesThread == nullptr)
    {
        return false;
    }

    uint32 VideoSourcesThreadId = this->VideoSourcesThread->GetThreadId();
    uint32 CurrentThreadId = FPlatformTLS::GetCurrentThreadId();

    return VideoSourcesThreadId == CurrentThreadId;
}

////////////////////////////////////////////
// INTERNAL VIDEO SOURCES THREAD FUNCTIONS
////////////////////////////////////////////

void FPixelStreamingVideoSources::RunVideoSourcesLoop_VideoSourcesThread()
{
    uint64 LastFrameSubmittedCycles = 0;

    while(this->bIsThreadRunning)
    {
        // Process any queued work
        while(!this->QueuedTasks.IsEmpty())
        {
            TFunction<void()>* Task = this->QueuedTasks.Peek();
            if(Task != nullptr && *Task)
            {
                (*Task)();
            }
            this->QueuedTasks.Pop();
        }

        // Submit Frame if we have a video source
        if(this->VideoSources.Num() > 0 && this->VideoCapturerContext.IsValid() && this->VideoCapturerContext->IsInitialized())
        {

            this->SubmitFrame_VideoSourcesThread();

            // Determine how long we should sleep before submitting a frame again
            uint64 DeltaCycles = FPlatformTime::Cycles64() - LastFrameSubmittedCycles;
            double DeltaMs = FPlatformTime::ToMilliseconds64(DeltaCycles);
            LastFrameSubmittedCycles = FPlatformTime::Cycles64();

            if(DeltaMs < this->FrameDelayMs)
            {
                // Sleep for DeltaMs or until ThreadWaiter.Trigger() is called.
                double SleepTimeMs = this->FrameDelayMs - DeltaMs;
                this->ThreadWaiter->Wait(SleepTimeMs, false);
            }
        }
        // No video sources, we will sleep until we have a video source or some queued work to do
        else
        {
            // Sleep indefinitely until ThreadWaiter.Trigger() is called somewhere.
            if(this->QueuedTasks.IsEmpty())
            {
                this->ThreadWaiter->Wait();
            }
        }
    }

}

void FPixelStreamingVideoSources::SubmitFrame_VideoSourcesThread()
{
    checkf(IsInVideoSourcesThread(), TEXT("This method must called on the VideoSourcesThread"));

    // Send the frame from the video source back into WebRTC where it will eventually makes its way into an encoder
    // Iterate each video source and initialize it, if needed.
    for(auto& Entry : this->VideoSources)
    {
        TUniquePtr<FVideoCapturer>& Capturer = Entry.Value;
        if(!Capturer->IsInitialized())
        {
            FIntPoint StartRes(this->VideoCapturerContext->GetCaptureWidth(), this->VideoCapturerContext->GetCaptureHeight());
            Capturer->Initialize(StartRes);
        }
    }

    TUniquePtr<FVideoCapturer>* ControllingVideoSourcePtr = this->VideoSources.Find(this->ControllingPlayer);

    // If there is no controlling player associated with any video source we should not transmit
    if(ControllingVideoSourcePtr == nullptr || (*ControllingVideoSourcePtr).IsValid() == false)
    {
        return;
    }

    // Pass along the capturer context to the controlling video source
    if((*ControllingVideoSourcePtr)->IsInitialized())
    {
        (*ControllingVideoSourcePtr)->TrySubmitFrame(this->VideoCapturerContext);
    }
}

void FPixelStreamingVideoSources::SetQualityController_VideoSourcesThread(FPlayerId PlayerId)
{
    checkf(IsInVideoSourcesThread(), TEXT("This method must called on the VideoSourcesThread"));

    if(this->VideoSources.Contains(PlayerId))
    {
        this->ControllingPlayer = PlayerId;
    }
    else
    {
        this->ControllingPlayer = INVALID_PLAYER_ID;
    }
}

webrtc::VideoTrackSourceInterface* FPixelStreamingVideoSources::CreateVideoSource_VideoSourcesThread(FPlayerId PlayerId)
{
    checkf(IsInVideoSourcesThread(), TEXT("This method must called on the VideoSourcesThread"));

    bool bShouldMakeController = this->VideoSources.Num() == 0;

    // Already have a source for this player id
    if(this->VideoSources.Contains(PlayerId))
    {
        return static_cast<webrtc::VideoTrackSourceInterface*>(this->VideoSources[PlayerId].Get());
    }

    TUniquePtr<FVideoCapturer> NewVideoSource = MakeUnique<FVideoCapturer>(PlayerId);
    webrtc::VideoTrackSourceInterface* WebRTCVideoSource = static_cast<webrtc::VideoTrackSourceInterface*>(NewVideoSource.Get());

    this->VideoSources.Emplace(PlayerId, MoveTemp(NewVideoSource));

    if(bShouldMakeController)
    {
        this->SetQualityController_VideoSourcesThread(PlayerId);
    }

    return WebRTCVideoSource;
}

void FPixelStreamingVideoSources::DeleteVideoSource_VideoSourcesThread(FPlayerId PlayerId)
{
    checkf(IsInVideoSourcesThread(), TEXT("This method must called on the VideoSourcesThread"));

    this->VideoSources.Remove(PlayerId);

    // If the controlling player is the video source that just got removed then make the controlling player invalid.
    if(this->ControllingPlayer == PlayerId)
    {
        this->ControllingPlayer = INVALID_PLAYER_ID;
    }
}
