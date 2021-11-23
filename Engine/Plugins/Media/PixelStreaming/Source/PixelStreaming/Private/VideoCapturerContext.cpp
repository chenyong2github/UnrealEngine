// Copyright Epic Games, Inc. All Rights Reserved.

#include "VideoCapturerContext.h"
#include "PixelStreamingSettings.h"
#include "CudaModule.h"
#include "VulkanRHIPrivate.h"
#include "Misc/CoreDelegates.h"
#include "PixelStreamingPrivate.h"
#include "LatencyTester.h"
#include "PixelStreamingStats.h"
#include "RHIResources.h"
#include "Async/Async.h"
#include "GenericPlatform/GenericPlatformProcess.h"

FVideoCapturerContext::FVideoCapturerContext(int InCaptureWidth, int InCaptureHeight, bool bInFixedResolution)
    : CaptureWidth(InCaptureWidth)
    , CaptureHeight(InCaptureHeight)
    , bFixedResolution(bInFixedResolution)
    , bIsInitialized(false)
{
	this->EvenFrame.CopyFence = GDynamicRHI->RHICreateGPUFence(TEXT("VideoCapturerCopyFenceEven"));
	this->OddFrame.CopyFence = GDynamicRHI->RHICreateGPUFence(TEXT("VideoCapturerCopyFenceOdd"));

	EvenFrame.Texture = MakeTexture();
	OddFrame.Texture = MakeTexture();
	TempTexture = MakeTexture();
	EncoderTexture = MakeTexture();
}

bool FVideoCapturerContext::IsFixedResolution() const
{
    return this->bFixedResolution;
}

int FVideoCapturerContext::GetCaptureWidth() const
{
    return this->CaptureWidth;
}

int FVideoCapturerContext::GetCaptureHeight() const
{
    return this->CaptureHeight;
}

int32 FVideoCapturerContext::GetNextFrameId()
{
    // todo: make this return uint16 and wrap when we reach max
    return this->NextFrameID.Increment();
}

bool FVideoCapturerContext::IsInitialized() const
{
    return this->bIsInitialized;
}

FTexture2DRHIRef FVideoCapturerContext::MakeTexture()
{
	FRHIResourceCreateInfo CreateInfo(TEXT("VideoCapturerBackBuffer"));
    FString RHIName = GDynamicRHI->GetName();

	// TODO check that these are being created correctly and that they are transitioned for copies
	return (RHIName == TEXT("Vulkan")) ?
			GDynamicRHI->RHICreateTexture2D(this->CaptureWidth, this->CaptureHeight, EPixelFormat::PF_B8G8R8A8, 1, 1, TexCreate_RenderTargetable | TexCreate_UAV, ERHIAccess::Present, CreateInfo) :
			GDynamicRHI->RHICreateTexture2D(this->CaptureWidth, this->CaptureHeight, EPixelFormat::PF_B8G8R8A8, 1, 1, TexCreate_Shared | TexCreate_RenderTargetable, ERHIAccess::CopyDest, CreateInfo);
}

void FVideoCapturerContext::SetCaptureResolution(int NewCaptureWidth, int NewCaptureHeight)
{
	// Don't change resolution if we are in a fixed resolution capturer or the user has indicated they do not want this behaviour.
	if(this->bFixedResolution || PixelStreamingSettings::CVarPixelStreamingWebRTCDisableResolutionChange.GetValueOnAnyThread())
	{
		return;
	}

	// Check is requested resolution is same as current resolution, if so, do nothing.
	if(this->CaptureWidth == NewCaptureWidth && this->CaptureHeight == NewCaptureHeight)
	{
		return;
	}

	verifyf(NewCaptureWidth > 0, TEXT("Capture width must be greater than zero."));
	verifyf(NewCaptureHeight > 0, TEXT("Capture height must be greater than zero."));

	this->CaptureWidth = NewCaptureWidth;
	this->CaptureHeight = NewCaptureHeight;
}

void FVideoCapturerContext::CaptureFrame(const FTexture2DRHIRef& FrameBuffer)
{
	// Latency test pre capture
	if(FLatencyTester::IsTestRunning() && FLatencyTester::GetTestStage() == FLatencyTester::ELatencyTestStage::PRE_CAPTURE)
	{
		FLatencyTester::RecordPreCaptureTime();
	}

	bIsEvenFrame = !bIsEvenFrame;
	FVideoCaptureFrame& Frame = bIsEvenFrame ? EvenFrame : OddFrame;

	Frame.CopyFence->Clear();

	FTexture2DRHIRef& DestinationTexture = Frame.Texture;
 
	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

	RHICmdList.EnqueueLambda([&](FRHICommandListImmediate& RHICmdList){
		Frame.PreWaitingOnCopy = FPlatformTime::Cycles64();
	});

	// Create a bunch of RHI commands to start a "copy" (render pass) on the render thread
	CopyTexture(FrameBuffer, DestinationTexture, Frame.CopyFence);

	AsyncTask(ENamedThreads::AnyHiPriThreadHiPriTask, [&]()
	{
		// Sleep this thread until the CopyFence is completed.
		FPlatformProcess::ConditionalSleep([Frame]()-> bool{ return Frame.CopyFence->Poll();}, TNumericLimits<float>::Max());
		
		{
			FScopeLock Lock(&CriticalSection);
			
			TempTexture.Swap(Frame.Texture);
			Frame.CopyFence->Clear();

			bIsTempDirty = true;
		}

		uint64 PostWaitingOnCopy = FPlatformTime::Cycles64();

		FPixelStreamingStats& Stats = FPixelStreamingStats::Get();
		if(Stats.GetStatsEnabled())
		{
			double CaptureLatencyMs = FPlatformTime::ToMilliseconds64(PostWaitingOnCopy - Frame.PreWaitingOnCopy);
			Stats.SetCaptureLatency(CaptureLatencyMs);
			Stats.OnCaptureFinished();
		}
	});

	bIsInitialized = true;
}

FTextureObtainer FVideoCapturerContext::RequestNewestCapturedFrame()
{
    checkf(bIsInitialized, TEXT("Cannot capture a frame if the capturer context is not initialized - is this being called before a frame has been rendered?"));

	return [this]() -> const FTexture2DRHIRef
	{
		if (bIsTempDirty)
		{
			FScopeLock Lock(&CriticalSection);

			EncoderTexture.Swap(TempTexture);
			
			bIsTempDirty = false;
		}

		return EncoderTexture;
	};
}
