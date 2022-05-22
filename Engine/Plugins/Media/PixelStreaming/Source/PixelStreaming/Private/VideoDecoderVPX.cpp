// Copyright Epic Games, Inc. All Rights Reserved.

#include "VideoDecoderVPX.h"

namespace UE::PixelStreaming
{
	VideoDecoderVPX::VideoDecoderVPX()
	{
		VideoDecoder = webrtc::VP8Decoder::Create();
	}

	int32 VideoDecoderVPX::InitDecode(const webrtc::VideoCodec* codec_settings, int32 number_of_cores)
	{
		return VideoDecoder->InitDecode(codec_settings, number_of_cores);
	}

	int32 VideoDecoderVPX::Decode(const webrtc::EncodedImage& input_image, bool missing_frames, int64_t render_time_ms)
	{
		return VideoDecoder->Decode(input_image, missing_frames, render_time_ms);
	}

	int32 VideoDecoderVPX::RegisterDecodeCompleteCallback(webrtc::DecodedImageCallback* callback)
	{
		return VideoDecoder->RegisterDecodeCompleteCallback(callback);
	}

	int32 VideoDecoderVPX::Release()
	{
		return VideoDecoder->Release();
	}
} // namespace UE::PixelStreaming
