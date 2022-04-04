// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TextureSourceBackbuffer.h"

namespace UE::PixelStreaming
{
	/*
	* Copies from the UE backbuffer and converts it to I420 using compute shaders
	*/
	class FTextureSourceComputeI420 : public FTextureSourceBackbuffer
	{
	public:
		virtual TSharedPtr<FPixelStreamingTextureWrapper> CreateBlankStagingTexture(uint32 Width, uint32 Height) override;
		virtual TSharedPtr<IPixelStreamingFrameCapturer> CreateFrameCapturer() override;
		virtual rtc::scoped_refptr<webrtc::I420Buffer> ToWebRTCI420Buffer(TSharedPtr<FPixelStreamingTextureWrapper> Texture) override;
	};

} // namespace UE::PixelStreaming
