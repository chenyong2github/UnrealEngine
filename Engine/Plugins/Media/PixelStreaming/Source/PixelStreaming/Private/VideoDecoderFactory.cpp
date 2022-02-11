// Copyright Epic Games, Inc. All Rights Reserved.
#include "VideoDecoderFactory.h"
#include "Utils.h"
#include "VideoDecoderStub.h"

namespace UE::PixelStreaming
{
	std::vector<webrtc::SdpVideoFormat> FVideoDecoderFactory::GetSupportedFormats() const
	{
		return { CreateH264Format(webrtc::H264::kProfileConstrainedBaseline, webrtc::H264::kLevel3_1) };
	}

	std::unique_ptr<webrtc::VideoDecoder> FVideoDecoderFactory::CreateVideoDecoder(const webrtc::SdpVideoFormat& format)
	{
		return std::make_unique<FVideoDecoderStub>();
	}
} // namespace UE::PixelStreaming
