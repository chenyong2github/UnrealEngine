// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TextureSourceBackbuffer.h"

namespace UE::PixelStreaming
{
	/*
	* Copies from the UE backbuffer into textures that are readback to CPU and system memory.
	*/
	class FTextureSourceCPUI420 : public FTextureSourceBackbuffer
	{
		virtual TSharedPtr<FPixelStreamingTextureWrapper> CreateBlankStagingTexture(uint32 Width, uint32 Height) override;
		virtual TSharedPtr<IPixelStreamingFrameCapturer> CreateFrameCapturer() override;
		virtual rtc::scoped_refptr<webrtc::I420Buffer> ToWebRTCI420Buffer(TSharedPtr<FPixelStreamingTextureWrapper> Texture) override;
	};
} // namespace UE::PixelStreaming
