// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "VideoCapturer.h"
#include "PlayerId.h"
#include "Containers/Map.h"
#include "RHIResources.h"
#include "Templates/UniquePtr.h"
#include "WebRTCIncludes.h"
#include "Math/IntPoint.h"

// All possible video sources that Pixel Streaming could send a framebuffer to.
// Currently this class is designed so only one video source is actively sent frames - however, this design may change is we support multiple frame buffers.
//
// NOTE: This class is threadsafe as all methods execute on the same thread - the rendering thread!
// The reason for executing work on the rendering thread is that in order to pass the framebuffer (via OnFrameReady) to WebRTC as fast as possible 
// we don't want to queue that work onto some other thread to run at a later time.
// Additionally, as we never want to lock the rendering thread when underlying video sources changes,
// our solution is to change underlying resources on render thread.
class FPixelStreamingVideoSources
{

    public:

        // Default constructor means video sources will try to match framebuffer size, even dynamically if a viewport is resized and stream resolution changing is permitted.
        FPixelStreamingVideoSources();

        // Specify the resolution of the video source, note if this constructor is used we assume resolution cannot be changed for this source now.
        FPixelStreamingVideoSources(int Width, int Height);

        void OnFrameReady(const FTexture2DRHIRef& FrameBuffer);
        webrtc::VideoTrackSourceInterface* CreateVideoSource(FPlayerId PlayerId);
        void DeleteVideoSource(FPlayerId PlayerId);

        // /*Quality Controller*/
        void SetQualityController(FPlayerId PlayerId);
        // FPlayerId GetQualityController();

    private:
        webrtc::VideoTrackSourceInterface* CreateVideoSource_RenderThread(FPlayerId PlayerId);
        void DeleteVideoSource_RenderThread(FPlayerId PlayerId);
        void SetQualityController_RenderThread(FPlayerId PlayerId);

    private:
        FPlayerId ControllingPlayer = INVALID_PLAYER_ID;
        TMap<FPlayerId, TUniquePtr<FVideoCapturer>> VideoSources;
        TSharedPtr<FVideoCapturerContext> VideoCapturerContext = nullptr;
        FIntPoint StartResolution;
        bool bMatchFrameBufferResolution;
        bool bFixedResolution;
};