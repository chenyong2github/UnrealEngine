// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VideoSourceBase.h"

class FPixelStreamingFrameAdapter;
class FPixelStreamingVideoInput;

namespace UE::PixelStreaming
{
	/*
	 * A video source for the SFU.
	 */
	class FVideoSourceSFU : public FVideoSourceBase
	{
	public:
		FVideoSourceSFU(TSharedPtr<FPixelStreamingVideoInput> InVideoInput);

		virtual bool IsReady() const override;

	protected:
		TSharedPtr<FPixelStreamingFrameAdapter> FrameAdapter;

	protected:
		virtual webrtc::VideoFrame CreateFrame(int32 FrameId) override;
	};
} // namespace UE::PixelStreaming
