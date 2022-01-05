// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WebRTCIncludes.h"
#include "PlayerId.h"

// each connected player has a single source. it is fed by FVideoSource and feeds the encoder
class FPlayerVideoSource : public rtc::AdaptedVideoTrackSource
{
public:
	FPlayerVideoSource(FPlayerId InPlayerId);
	virtual ~FPlayerVideoSource();

	FPlayerId GetPlayerId() const { return PlayerId; }
	bool IsInitialised() const { return Initialised; }

	void AddRef() const override
	{
		FPlatformAtomics::InterlockedIncrement(&RefCount);
	}

	rtc::RefCountReleaseStatus Release() const override
	{
		if (FPlatformAtomics::InterlockedDecrement(&RefCount) == 0)
		{
			return rtc::RefCountReleaseStatus::kDroppedLastRef;
		}

		return rtc::RefCountReleaseStatus::kOtherRefsRemained;
	}

	void OnFrameReady(const webrtc::VideoFrame& Frame);

	virtual webrtc::MediaSourceInterface::SourceState state() const override
	{
		return CurrentState;
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
	bool AdaptCaptureFrame(const int64 TimestampUs, FIntPoint Resolution);

	webrtc::MediaSourceInterface::SourceState CurrentState;

	mutable int32 RefCount;
	FPlayerId PlayerId;
	bool Initialised = false;
};
