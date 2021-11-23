// Copyright Epic Games, Inc. All Rights Reserved.

#include "VideoCapturer.h"
#include "Utils.h"
#include "Engine/Engine.h"
#include "CommonRenderResources.h"
#include "GlobalShader.h"
#include "HAL/PlatformTime.h"
#include "Misc/Timespan.h"
#include "Modules/ModuleManager.h"
#include "PixelStreamingSettings.h"
#include "PixelStreamingFrameBuffer.h"
#include "PixelStreamingStats.h"
#include "LatencyTester.h"

FVideoCapturer::FVideoCapturer(FPlayerId InPlayerId)
	: bInitialized(false)
	, PlayerId(InPlayerId)

{
	CurrentState = webrtc::MediaSourceInterface::SourceState::kInitializing;
}

FVideoCapturer::~FVideoCapturer()
{
	CurrentState = webrtc::MediaSourceInterface::SourceState::kEnded;
	UE_LOG(PixelStreamer, Log, TEXT("Destroying WebRTC video track source associated with player: %s."), *PlayerId);
}

bool FVideoCapturer::IsInitialized()
{
	return this->bInitialized;
}

void FVideoCapturer::Initialize(FIntPoint& StartResolution)
{
	// Check if already initialized
	if(this->bInitialized)
	{
		return;
	}

	if(CurrentState != webrtc::MediaSourceInterface::SourceState::kLive)
	{
		CurrentState = webrtc::MediaSourceInterface::SourceState::kLive;
	}

	// Here we pass the frame to Webrtc which will pass it to the correct encoder.
	// We pass a special frame called the FPixelStreamingInitFrameBuffer that contains the player id, so the encoder is associated with a specific player id.
	rtc::scoped_refptr<FPixelStreamingInitFrameBuffer> Buffer = new rtc::RefCountedObject<FPixelStreamingInitFrameBuffer>(
		PlayerId, 
		StartResolution.X, 
		StartResolution.Y);

	// Bind to when encoder is actually intialized as we will use this as a signal to start sending proper frames for encoding
	Buffer->OnEncoderInitialized.BindRaw(this, &FVideoCapturer::OnEncoderInitialized);

	// Build a WebRTC frame (note frame id hardcoded to zero, we assume this is okay)
	webrtc::VideoFrame Frame = webrtc::VideoFrame::Builder()
		.set_video_frame_buffer(Buffer)
		.set_timestamp_us(rtc::TimeMicros())
		.set_rotation(webrtc::VideoRotation::kVideoRotation_0)
		.set_id(0).build();

	// Pass the frame to WebRTC, where is will eventually end up being processed by an encoder
	OnFrame(Frame);
}

bool FVideoCapturer::TrySubmitFrame(TSharedPtr<FVideoCapturerContext, ESPMode::ThreadSafe> CapturerContext)
{
	FPixelStreamingStats& Stats = FPixelStreamingStats::Get();

	const int64 TimestampUs = rtc::TimeMicros();

	if(!AdaptCaptureFrame(TimestampUs, CapturerContext))
	{
		return false;
	}

	// Generate a new ID for this frame
	int32 FrameId = CapturerContext->GetNextFrameId();

	// Latency test post capture
	if(FLatencyTester::IsTestRunning() && FLatencyTester::GetTestStage() == FLatencyTester::ELatencyTestStage::POST_CAPTURE)
	{
		FLatencyTester::RecordPostCaptureTime(FrameId);
	}

	FTextureObtainer TextureObtainer = CapturerContext->RequestNewestCapturedFrame();

    // Here we pass the frame to Webrtc which will pass it to the correct encoder.
    rtc::scoped_refptr<FPixelStreamingFrameBuffer> Buffer = new rtc::RefCountedObject<FPixelStreamingFrameBuffer>(TextureObtainer, CapturerContext->GetCaptureWidth(), CapturerContext->GetCaptureHeight());
    webrtc::VideoFrame WebRTCFrame = webrtc::VideoFrame::Builder()
        .set_video_frame_buffer(Buffer)
        .set_timestamp_us(TimestampUs)
        .set_rotation(webrtc::VideoRotation::kVideoRotation_0)
        .set_id(FrameId)
        .build();

	OnFrame(WebRTCFrame);

	// Record stat for how often we are submitting to WebRTC
	if (Stats.GetStatsEnabled())
	{
		Stats.OnFrameSubmittedToWebRTC();
	}
	
	return true;
}

void FVideoCapturer::OnEncoderInitialized()
{
	this->bInitialized = true;
}

bool FVideoCapturer::AdaptCaptureFrame(const int64 TimestampUs, TSharedPtr<FVideoCapturerContext, ESPMode::ThreadSafe> CapturerContext)
{
	bool bWebRTCWantsTheFrame = true;

	int outWidth, outHeight, cropWidth, cropHeight, cropX, cropY;
	if(!AdaptFrame(CapturerContext->GetCaptureWidth(), CapturerContext->GetCaptureHeight(), TimestampUs, &outWidth, &outHeight, &cropWidth, &cropHeight, &cropX, &cropY))
	{
		bWebRTCWantsTheFrame = false;
	}

	FIntPoint PreferredCaptureRes;

	// Set Resolution of encoder using user-defined params (i.e. not the back buffer).
	if(!PixelStreamingSettings::CVarPixelStreamingUseBackBufferCaptureSize.GetValueOnAnyThread())
	{
		// set Resolution based on cvars
		FString CaptureSize = PixelStreamingSettings::CVarPixelStreamingCaptureSize.GetValueOnAnyThread();
		FString TargetWidth, TargetHeight;
		bool bValidSize = CaptureSize.Split(TEXT("x"), &TargetWidth, &TargetHeight);
		if(bValidSize)
		{
			PreferredCaptureRes.X = FCString::Atoi(*TargetWidth);
			PreferredCaptureRes.Y = FCString::Atoi(*TargetHeight);
		}
		else
		{
			UE_LOG(PixelStreamer, Error, TEXT("CVarPixelStreamingCaptureSize is not in a valid format: %s. It should be e.g: \"1920x1080\""), *CaptureSize);
			PixelStreamingSettings::CVarPixelStreamingCaptureSize->Set(*FString::Printf(TEXT("%dx%d"), PreferredCaptureRes.X, PreferredCaptureRes.Y));
		}
	}
	else
	{
		PreferredCaptureRes.X = outWidth;
		PreferredCaptureRes.Y = outHeight;
	}

	if(PreferredCaptureRes.X != CapturerContext->GetCaptureWidth() || PreferredCaptureRes.Y != CapturerContext->GetCaptureHeight())
	{
		CapturerContext->SetCaptureResolution(PreferredCaptureRes.X, PreferredCaptureRes.Y);
	}

	return bWebRTCWantsTheFrame;
}
