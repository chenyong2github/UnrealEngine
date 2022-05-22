// Copyright Epic Games, Inc. All Rights Reserved.

#include "VideoSourceBase.h"

namespace UE::PixelStreaming
{
	FVideoSourceBase::FVideoSourceBase()
	{
		CurrentState = webrtc::MediaSourceInterface::SourceState::kInitializing;
	}

	FVideoSourceBase::~FVideoSourceBase()
	{
		CurrentState = webrtc::MediaSourceInterface::SourceState::kEnded;
	}

	void FVideoSourceBase::PushFrame()
	{
		static int32 sFrameId = 1;

		CurrentState = webrtc::MediaSourceInterface::SourceState::kLive;

		webrtc::VideoFrame Frame = CreateFrame(sFrameId++);

		// might want to allow source frames to be scaled here?
		const FIntPoint Resolution(Frame.width(), Frame.height());
		if (!AdaptCaptureFrame(Frame.timestamp_us(), Resolution))
		{
			return;
		}

		OnFrame(Frame);
	}

	bool FVideoSourceBase::AdaptCaptureFrame(const int64 TimestampUs, FIntPoint Resolution)
	{
		// disabled for now since we want to control framerate directly

		//int outWidth, outHeight, cropWidth, cropHeight, cropX, cropY;
		//if (!AdaptFrame(Resolution.X, Resolution.Y, TimestampUs, &outWidth, &outHeight, &cropWidth, &cropHeight, &cropX, &cropY))
		//{
		//	return false;
		//}

		return true;
	}
} // namespace UE::PixelStreaming
