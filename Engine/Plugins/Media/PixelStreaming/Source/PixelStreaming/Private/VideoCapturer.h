// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingPrivate.h"
#include "RHI.h"
#include "VideoEncoderInput.h"

// This is a video track source for WebRTC.
// Its main purpose is to copy frames from the Unreal Engine backbuffer.
class FVideoCapturer : public rtc::AdaptedVideoTrackSource
{
public:
	FVideoCapturer();

	void OnFrameReady(const FTexture2DRHIRef& FrameBuffer);

	void AddRef() const override
	{
		FPlatformAtomics::InterlockedIncrement(const_cast<volatile int32*>(&count));
	}

	rtc::RefCountReleaseStatus Release() const override
	{

		return FPlatformAtomics::InterlockedDecrement(const_cast<volatile int32*>(&count)) == 0
			? rtc::RefCountReleaseStatus::kDroppedLastRef
			: rtc::RefCountReleaseStatus::kOtherRefsRemained;
	}

	virtual webrtc::MediaSourceInterface::SourceState state() const override
	{
		return this->CurrentState;
	}

	virtual bool remote() const override
	{
		return false;
	}

	virtual bool is_screencast() const override
	{
		return false;
	}

	virtual absl::optional<bool> needs_denoising() const override
	{
		return false;
	}

private:
	AVEncoder::FVideoEncoderInputFrame* ObtainInputFrame();
	void CopyTexture(const FTexture2DRHIRef& SourceTexture, FTexture2DRHIRef& DestinationTexture) const;
	bool AdaptCaptureFrame(const int64 TimestampUs, FIntPoint Resolution);
	void SetCaptureResolution(int width, int height);
	
	TMap<AVEncoder::FVideoEncoderInputFrame*, FTexture2DRHIRef> BackBuffers;
	TSharedPtr<AVEncoder::FVideoEncoderInput> VideoEncoderInput = nullptr;

	int32 Width = 1920;
	int32 Height = 1080;
	int32 Framerate = 60;
	webrtc::MediaSourceInterface::SourceState CurrentState;
	volatile int32 count;
};
