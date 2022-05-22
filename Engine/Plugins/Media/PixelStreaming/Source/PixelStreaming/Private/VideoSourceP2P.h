// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VideoSourceBase.h"

class FPixelStreamingFrameAdapter;
class FPixelStreamingVideoInput;

namespace UE::PixelStreaming
{
	/*
	 * A video source for P2P peers.
	 */
	class FVideoSourceP2P : public FVideoSourceBase
	{
	public:
		FVideoSourceP2P(TSharedPtr<FPixelStreamingVideoInput> InVideoInput, TFunction<bool()> InIsQualityControllerFunc);

		virtual bool IsReady() const override;

	protected:
		TSharedPtr<FPixelStreamingFrameAdapter> FrameAdapter;
		TFunction<bool()> IsQualityControllerFunc;

	protected:
		virtual webrtc::VideoFrame CreateFrame(int32 FrameId) override;
	};
} // namespace UE::PixelStreaming
