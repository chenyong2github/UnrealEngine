// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingPrivate.h"
#include "VideoEncoderInput.h"

using SourceState = webrtc::MediaSourceInterface::SourceState;

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

	// Return true is resolution was changed.
	bool SetCaptureResolution(int width, int height);

private:
	void CopyTexture(const FTexture2DRHIRef& SourceTexture, FTexture2DRHIRef& DestinationTexture);
	
	virtual SourceState state() const override 
	{ 
		return this->CurrentState; 
	};

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

	TMap<AVEncoder::FVideoEncoderInputFrame*, FTexture2DRHIRef> BackBuffers;
	TSharedPtr<AVEncoder::FVideoEncoderInput> VideoEncoderInput = nullptr;
	int64 LastTimestampUs = 0;

	int32 Width = 1920;
	int32 Height = 1080;
	int32 Framerate = 60;
	SourceState CurrentState;
	volatile int32 count;
};
