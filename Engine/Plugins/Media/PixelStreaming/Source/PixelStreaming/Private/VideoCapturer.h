// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingPrivate.h"
#include "WebRTCIncludes.h"
#include "PlayerId.h"
#include "VideoCapturerContext.h"

// This is a video track source for WebRTC.
// Its main purpose is to copy frames from the Unreal Engine backbuffer.
class FVideoCapturer : public rtc::AdaptedVideoTrackSource
{
public:
	FVideoCapturer(FPlayerId InPlayerId);
	~FVideoCapturer();

	bool IsInitialized();
	void Initialize(FIntPoint& StartResolution);
	bool TrySubmitFrame(TSharedPtr<FVideoCapturerContext, ESPMode::ThreadSafe> CapturerContext);

	void AddRef() const override
	{
		FPlatformAtomics::InterlockedIncrement(const_cast<volatile int32*>(&Count));
	}

	rtc::RefCountReleaseStatus Release() const override
	{
		if (FPlatformAtomics::InterlockedDecrement(const_cast<volatile int32*>(&Count)) == 0)
		{
			return rtc::RefCountReleaseStatus::kDroppedLastRef;
		}
		
		return rtc::RefCountReleaseStatus::kOtherRefsRemained;
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
		return true;
	}

	virtual absl::optional<bool> needs_denoising() const override
	{
		return false;
	}

private:
	bool AdaptCaptureFrame(const int64 TimestampUs, TSharedPtr<FVideoCapturerContext, ESPMode::ThreadSafe> CapturerContext);
	void SetCaptureResolution(int width, int height);
	void OnEncoderInitialized();

	bool bInitialized;
	FPlayerId PlayerId;
	webrtc::MediaSourceInterface::SourceState CurrentState;
	volatile int32 Count;
};
