// Copyright Epic Games, Inc. All Rights Reserved.
#include "PixelStreamingVideoSources.h"
#include "CoreGlobals.h"
#include "Misc/ScopedEvent.h"
#include "VulkanRHIPrivate.h"
#include "PixelStreamingSettings.h"
#include "Async/Async.h"

#define SUBMIT_RENDERING_TASK_WITH_PARAMS(Func, ...) \
    if(IsInRenderingThread()) \
    { \
        this->Func(__VA_ARGS__); \
    } \
    else \
    { \
        AsyncTask(ENamedThreads::ActualRenderingThread, [this, __VA_ARGS__](){ this->Func(__VA_ARGS__); } ); \
    } \

FPixelStreamingVideoSources::FPixelStreamingVideoSources()
    : bMatchFrameBufferResolution(true)
    , bFixedResolution(PixelStreamingSettings::CVarPixelStreamingWebRTCDisableResolutionChange.GetValueOnAnyThread()){}

FPixelStreamingVideoSources::FPixelStreamingVideoSources(int Width, int Height)
    : StartResolution(FIntPoint(Width, Height))
    , bMatchFrameBufferResolution(false)
    , bFixedResolution(true){}

webrtc::VideoTrackSourceInterface* FPixelStreamingVideoSources::CreateVideoSource(FPlayerId PlayerId)
{
    if(IsInRenderingThread())
    {
        return this->CreateVideoSource_RenderThread(PlayerId);
    }
    else
    {
        webrtc::VideoTrackSourceInterface* VideoSource = nullptr;

        // Start scoped thread blocker here
        {
            FScopedEvent ThreadBlocker;

            AsyncTask(ENamedThreads::ActualRenderingThread, [this, PlayerId, &VideoSource, &ThreadBlocker]()
            {
                VideoSource = this->CreateVideoSource_RenderThread(PlayerId);

                // Unblocks the thread
                ThreadBlocker.Trigger();
            });

            // Blocks current thread until render thread calls trigger (indicating it is done)
        }

        return VideoSource;
    }
}

void FPixelStreamingVideoSources::SetQualityController(FPlayerId PlayerId)
{
    SUBMIT_RENDERING_TASK_WITH_PARAMS(SetQualityController_RenderThread, PlayerId)
}

void FPixelStreamingVideoSources::DeleteVideoSource(FPlayerId PlayerId)
{
    SUBMIT_RENDERING_TASK_WITH_PARAMS(DeleteVideoSource_RenderThread, PlayerId)
}

void FPixelStreamingVideoSources::OnFrameReady(const FTexture2DRHIRef& FrameBuffer)
{
    checkf(IsInRenderingThread(), TEXT("This method must be called on the rendering thread."));

    // There is no controlling player, so discard the incoming frame as this implies there are no video sources.
    if(this->ControllingPlayer == INVALID_PLAYER_ID)
    {
        return;
    }

    // If we have not created video encoder input yet, do it here.
    if(!this->VideoCapturerContext.IsValid())
    {
        // Make FVideoEncoderInput at the appropriate resolution based on VideoSource settings
        this->StartResolution = this->bMatchFrameBufferResolution ? FrameBuffer->GetSizeXY() : this->StartResolution;
        this->VideoCapturerContext = MakeShared<FVideoCapturerContext>(this->StartResolution.X, this->StartResolution.Y, this->bFixedResolution);
    }

    // Iterate each video source and initialize it, if needed.
    for(auto& Entry : this->VideoSources)
    {
        TUniquePtr<FVideoCapturer>& Capturer = Entry.Value;
        if(!Capturer->IsInitialized())
        {
            Capturer->Initialize(FrameBuffer, this->VideoCapturerContext);
        }
    }

    TUniquePtr<FVideoCapturer>& ControllingVideoSource = this->VideoSources.FindChecked(this->ControllingPlayer);

    checkf(ControllingVideoSource.IsValid(), TEXT("Video source was invalid, this indicates a bug and should never happen."));

    // Pass along the frame buffer to the controlling video source
    if(ControllingVideoSource->IsInitialized())
    {
        ControllingVideoSource->OnFrameReady(FrameBuffer);
    }

}

///////////////////////
// INTERNAL
///////////////////////

void FPixelStreamingVideoSources::SetQualityController_RenderThread(FPlayerId PlayerId)
{
    checkf(IsInRenderingThread(), TEXT("This method must be called on the rendering thread."));

    if(this->VideoSources.Contains(PlayerId))
    {
        this->ControllingPlayer = PlayerId;
    }
    else
    {
        this->ControllingPlayer = INVALID_PLAYER_ID;
    }
}

webrtc::VideoTrackSourceInterface* FPixelStreamingVideoSources::CreateVideoSource_RenderThread(FPlayerId PlayerId)
{
    checkf(IsInRenderingThread(), TEXT("This method must be called on the rendering thread."));

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
        this->SetQualityController_RenderThread(PlayerId);
    }

    return WebRTCVideoSource;
}

void FPixelStreamingVideoSources::DeleteVideoSource_RenderThread(FPlayerId PlayerId)
{
    checkf(IsInRenderingThread(), TEXT("This method must be called on the rendering thread."));

    this->VideoSources.Remove(PlayerId);

    // If the controlling player is the video source that just got removed then make the controlling player invalid.
    if(this->ControllingPlayer == PlayerId)
    {
        this->ControllingPlayer = INVALID_PLAYER_ID;
    }
}