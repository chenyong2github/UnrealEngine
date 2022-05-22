// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AdaptedVideoTrackSource.h"

namespace UE::PixelStreaming
{
	/*
	 * Video sources are WebRTC video sources that populate WebRTC tracks
	 * and pass WebRTC video frames to `OnFrame`, which eventually gets passed to a WebRTC video encoder, encoded, and transmitted.
	 */
	class FVideoSourceBase : public FAdaptedVideoTrackSource
	{
	public:
		FVideoSourceBase();
		virtual ~FVideoSourceBase();

		/* Begin UE::PixelStreaming::AdaptedVideoTrackSource overrides */
		virtual webrtc::MediaSourceInterface::SourceState state() const override { return CurrentState; }
		virtual bool remote() const override { return false; }
		virtual bool is_screencast() const override { return false; }
		virtual absl::optional<bool> needs_denoising() const override { return false; }
		/* End UE::PixelStreaming::AdaptedVideoTrackSource overrides */

		virtual bool IsReady() const = 0;
		void PushFrame();

	protected:
		virtual bool AdaptCaptureFrame(const int64 TimestampUs, FIntPoint Resolution);
		virtual webrtc::VideoFrame CreateFrame(int32 FrameId) = 0;

	protected:
		webrtc::MediaSourceInterface::SourceState CurrentState;
	};
} // namespace UE::PixelStreaming
