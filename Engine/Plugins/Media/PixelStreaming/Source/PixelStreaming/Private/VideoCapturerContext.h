// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/Optional.h"
#include "VideoEncoderInput.h"
#include "RHIStaticStates.h"
#include "RHIDefinitions.h"
#include "RHI.h"
#include "RenderResource.h"
#include "ScreenRendering.h"
#include "PixelStreamingFrameBuffer.h"
#include "Containers/TripleBuffer.h"

struct FVideoCaptureFrame
{
    FGPUFenceRHIRef CopyFence;
    FTexture2DRHIRef Texture;
	uint64 PreWaitingOnCopy;
};


// Context object for `FVideoCapturer`. The context contains backbuffers and the appropriates objects/methods to create frames for submission to 
// the hardware encoders. The context is split out from the `FVideoCapturer` itself because context can be shared/passed around to capturers as they become active.
class FVideoCapturerContext
{

    public:
        FVideoCapturerContext(int InCaptureWidth, int InCaptureHeight, bool bInFixedResolution);
        int GetCaptureWidth() const;
        int GetCaptureHeight() const;
        bool IsFixedResolution() const;
        void SetCaptureResolution(int NewCaptureWidth, int NewCaptureHeight);
        int32 GetNextFrameId();
        void CaptureFrame(const FTexture2DRHIRef& FrameBuffer);
        FTextureObtainer RequestNewestCapturedFrame();
        bool IsInitialized() const;

    private:
        FTexture2DRHIRef MakeTexture();
        void Shutdown();
        void DeleteBackBuffers();
		void SwapBackBuffers();

    private:
        int CaptureWidth;
        int CaptureHeight;
        bool bFixedResolution;

        FVideoCaptureFrame EvenFrame;
        FVideoCaptureFrame OddFrame;
        FTexture2DRHIRef TempTexture;
        FTexture2DRHIRef EncoderTexture;

		FThreadSafeBool bIsTempDirty;

        FThreadSafeCounter NextFrameID;
        FThreadSafeBool bIsInitialized;
        FCriticalSection CriticalSection;

        bool bIsEvenFrame;
};
