// Copyright Epic Games, Inc. All Rights Reserved.

#include "VideoCapturer.h"
#include "Utils.h"
#include "Engine/Engine.h"
#include "CommonRenderResources.h"
#include "GlobalShader.h"
#include "HAL/PlatformTime.h"
#include "Misc/Timespan.h"
#include "Modules/ModuleManager.h"
#include "PixelStreamingFrameBuffer.h"
#include "PixelStreamingSettings.h"
#include "PixelStreamingStats.h"
#include "LatencyTester.h"
#include "VideoCapturerContext.h"

FVideoCapturer::FVideoCapturer(FPlayerId InPlayerId)
	: bInitialized(false)
	, PlayerId(InPlayerId)

{
	CurrentState = webrtc::MediaSourceInterface::SourceState::kInitializing;
}

FVideoCapturer::~FVideoCapturer()
{
	CurrentState = webrtc::MediaSourceInterface::SourceState::kEnded;
}

bool FVideoCapturer::IsInitialized()
{
	return this->bInitialized;
}

void FVideoCapturer::Initialize(const FTexture2DRHIRef& FrameBuffer, TSharedPtr<FVideoCapturerContext> InCapturerContext)
{
	// Check if already initialized
	if(this->bInitialized)
	{
		return;
	}

	this->CapturerContext = InCapturerContext;

	const int64 TimestampUs = rtc::TimeMicros();
	if(!AdaptCaptureFrame(TimestampUs, FrameBuffer->GetSizeXY()))
	{
		return;
	}

	// Here we pass the frame to Webrtc which will pass it to the correct encoder.
	// We pass a special frame called the FPixelStreamingInitFrameBuffer that contains the player id, so the encoder is associated with a specific player id.
	rtc::scoped_refptr<FPixelStreamingInitFrameBuffer> Buffer = new rtc::RefCountedObject<FPixelStreamingInitFrameBuffer>(
		PlayerId, 
		this->CapturerContext->GetCaptureWidth(), 
		this->CapturerContext->GetCaptureHeight());

	// Bind to when encoder is actually intialized as we will use this as a signal to start sending proper frames for encoding
	Buffer->OnEncoderInitialized.BindRaw(this, &FVideoCapturer::OnEncoderInitialized);

	// Build a WebRTC frame (note frame id hardcoded to zero, we assume this is okay)
	webrtc::VideoFrame Frame = webrtc::VideoFrame::Builder()
		.set_video_frame_buffer(Buffer)
		.set_timestamp_us(TimestampUs)
		.set_rotation(webrtc::VideoRotation::kVideoRotation_0)
		.set_id(0).build();

	// Pass the frame to WebRTC, where is will eventually end up being processed by an encoder
	OnFrame(Frame);
}

void FVideoCapturer::OnEncoderInitialized()
{
	this->bInitialized = true;
}

void FVideoCapturer::OnFrameReady(const FTexture2DRHIRef& FrameBuffer)
{
	
	if(!this->bInitialized)
	{
		// Not initialized, can't do anything with this framebuffer
		return;
	}

	const int64 TimestampUs = rtc::TimeMicros();
	if(!AdaptCaptureFrame(TimestampUs, FrameBuffer->GetSizeXY()))
	{
		return;
	}

	if(CurrentState != webrtc::MediaSourceInterface::SourceState::kLive)
	{
		CurrentState = webrtc::MediaSourceInterface::SourceState::kLive;
	}
	
	FVideoCapturerContext::FCapturerInput CapturerInput = this->CapturerContext->ObtainCapturerInput();

	if(CapturerInput.InputFrame == nullptr || !CapturerInput.Texture.IsSet())
	{
		return;
	}

	AVEncoder::FVideoEncoderInputFrame* InputFrame = CapturerInput.InputFrame;
	FTexture2DRHIRef Texture = CapturerInput.Texture.GetValue();

	const int32 FrameId = InputFrame->GetFrameID();
	InputFrame->SetTimestampUs(TimestampUs);

	// Latency test pre capture
	if(FLatencyTester::IsTestRunning() && FLatencyTester::GetTestStage() == FLatencyTester::ELatencyTestStage::PRE_CAPTURE)
	{
		FLatencyTester::RecordPreCaptureTime(FrameId);
	}

	// Actual texture copy (i.e the actual "capture")
	CopyTexture(FrameBuffer, Texture);

	// Latency test post capture
	if(FLatencyTester::IsTestRunning() && FLatencyTester::GetTestStage() == FLatencyTester::ELatencyTestStage::POST_CAPTURE)
	{
		FLatencyTester::RecordPostCaptureTime(FrameId);
	}

	UE_LOG(PixelStreamer, VeryVerbose, TEXT("(%d) captured video %lld"), RtcTimeMs(), TimestampUs);

	// Here we pass the frame to Webrtc which will pass it to the correct encoder.
	rtc::scoped_refptr<FPixelStreamingFrameBuffer> Buffer = new rtc::RefCountedObject<FPixelStreamingFrameBuffer>(Texture, InputFrame, this->CapturerContext->GetVideoEncoderInput());
	webrtc::VideoFrame Frame = webrtc::VideoFrame::Builder()
		.set_video_frame_buffer(Buffer)
		.set_timestamp_us(TimestampUs)
		.set_rotation(webrtc::VideoRotation::kVideoRotation_0)
		.set_id(FrameId)
		.build();
	
	// Send the frame from the video source back into WebRTC where it will eventually makes its way into an encoder
	OnFrame(Frame);

	InputFrame->Release();

	// If stats are enabled, records the stats during capture now.
	FPixelStreamingStats& Stats = FPixelStreamingStats::Get();
	if(Stats.GetStatsEnabled())
	{
		int64 TimestampNowUs = rtc::TimeMicros();
		int64 CaptureLatencyUs = TimestampNowUs - TimestampUs;
		double CaptureLatencyMs = (double)CaptureLatencyUs / 1000.0;
		Stats.SetCaptureLatency(CaptureLatencyMs);
		Stats.OnCaptureFinished();
	}

}



bool FVideoCapturer::AdaptCaptureFrame(const int64 TimestampUs, FIntPoint Resolution)
{
	int outWidth, outHeight, cropWidth, cropHeight, cropX, cropY;
	if(!AdaptFrame(Resolution.X, Resolution.Y, TimestampUs, &outWidth, &outHeight, &cropWidth, &cropHeight, &cropX, &cropY))
	{
		return false;
	}

	// Set resolution of encoder using user-defined params (i.e. not the back buffer).
	if(!PixelStreamingSettings::CVarPixelStreamingUseBackBufferCaptureSize.GetValueOnRenderThread())
	{
		// set resolution based on cvars
		FString CaptureSize = PixelStreamingSettings::CVarPixelStreamingCaptureSize.GetValueOnRenderThread();
		FString TargetWidth, TargetHeight;
		bool bValidSize = CaptureSize.Split(TEXT("x"), &TargetWidth, &TargetHeight);
		if(bValidSize)
		{
			Resolution.X = FCString::Atoi(*TargetWidth);
			Resolution.Y = FCString::Atoi(*TargetHeight);
		}
		else
		{
			UE_LOG(PixelStreamer, Error, TEXT("CVarPixelStreamingCaptureSize is not in a valid format: %s. It should be e.g: \"1920x1080\""), *CaptureSize);
			PixelStreamingSettings::CVarPixelStreamingCaptureSize->Set(*FString::Printf(TEXT("%dx%d"), Resolution.X, Resolution.Y));
		}
	}
	else
	{
		Resolution.X = outWidth;
		Resolution.Y = outHeight;
	}

	this->CapturerContext->SetCaptureResolution(Resolution.X, Resolution.Y);

	return true;
}
