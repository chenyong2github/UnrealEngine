// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "VideoCapturer.h"
#include "PlayerId.h"
#include "Containers/Map.h"
#include "RHIResources.h"
#include "Templates/UniquePtr.h"
#include "WebRTCIncludes.h"
#include "Math/IntPoint.h"
#include "HAL/Thread.h"

// All possible video sources that Pixel Streaming could send a framebuffer to.
// Currently this class is designed so only one video source is actively sent frames - however, this design may change is we support multiple frame buffers.
//
// NOTE: This class is threadsafe as all public methods execute on the same thread - the "video sources thread".
class FPixelStreamingVideoSources
{

    public:

        // Default constructor means video sources will try to match framebuffer size, even dynamically if a viewport is resized and stream resolution changing is permitted.
        FPixelStreamingVideoSources();

        // Specify the resolution of the video source, note if this constructor is used we assume resolution cannot be changed for this source now.
        FPixelStreamingVideoSources(int Width, int Height);

        ~FPixelStreamingVideoSources();

        void OnFrameReady(const FTexture2DRHIRef& FrameBuffer);
        webrtc::VideoTrackSourceInterface* CreateVideoSource(FPlayerId PlayerId);
        void DeleteVideoSource(FPlayerId PlayerId);

        // /*Quality Controller*/
        void SetQualityController(FPlayerId PlayerId);

    /*Functions that run exclusively on the VideoSourcesThread*/
    private:
        webrtc::VideoTrackSourceInterface* CreateVideoSource_VideoSourcesThread(FPlayerId PlayerId);
        void DeleteVideoSource_VideoSourcesThread(FPlayerId PlayerId);
        void SetQualityController_VideoSourcesThread(FPlayerId PlayerId);
        void RunVideoSourcesLoop_VideoSourcesThread();
        void SubmitFrame_VideoSourcesThread();

    private:
        bool IsInVideoSourcesThread();
        void QueueTask(TFunction<void()> InTaskToQueue);

    private:
        FPlayerId ControllingPlayer = INVALID_PLAYER_ID;
        TMap<FPlayerId, TUniquePtr<FVideoCapturer>> VideoSources;
        TSharedPtr<FVideoCapturerContext, ESPMode::ThreadSafe> VideoCapturerContext = nullptr;
        FIntPoint StartResolution;
        bool bMatchFrameBufferResolution;
        bool bFixedResolution;
        TUniquePtr<FThread> VideoSourcesThread;
        FEvent* ThreadWaiter;
        TQueue<TFunction<void()>> QueuedTasks;
        FThreadSafeBool bIsThreadRunning;
		float FrameDelayMs = (1.0 / 60.0f) * 1000.0f;
};