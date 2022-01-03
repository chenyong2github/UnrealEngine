// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayerVideoSource.h"

FPlayerVideoSource::FPlayerVideoSource(FPlayerId InPlayerId)
	: PlayerId(InPlayerId)
{
	CurrentState = webrtc::MediaSourceInterface::SourceState::kInitializing;
}

FPlayerVideoSource::~FPlayerVideoSource()
{
	CurrentState = webrtc::MediaSourceInterface::SourceState::kEnded;
}

void FPlayerVideoSource::OnFrameReady(const webrtc::VideoFrame& Frame)
{
	// this mostly just forwards on to OnFrame.
	// we dont call it directly mostly so we can call AdaptCaptureFrame and possibly
	// drop a frame if requested. webrtc might also request a change in resolution
	// which we do not support right now.

	CurrentState = webrtc::MediaSourceInterface::SourceState::kLive;

	// might want to allow source frames to be scaled here?
	const FIntPoint Resolution(Frame.width(), Frame.height());
	if (!AdaptCaptureFrame(Frame.timestamp_us(), Resolution))
	{
		return;
	}

	OnFrame(Frame);

	Initialised = true;
}

bool FPlayerVideoSource::AdaptCaptureFrame(const int64 TimestampUs, FIntPoint Resolution)
{
	int outWidth, outHeight, cropWidth, cropHeight, cropX, cropY;
	if (!AdaptFrame(Resolution.X, Resolution.Y, TimestampUs, &outWidth, &outHeight, &cropWidth, &cropHeight, &cropX, &cropY))
	{
		return false;
	}

	return true;
}
