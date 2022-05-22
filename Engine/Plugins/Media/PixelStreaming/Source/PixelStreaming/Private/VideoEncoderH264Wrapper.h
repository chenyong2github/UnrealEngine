// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "VideoEncoder.h"
#include "EncoderFrameFactory.h"

namespace webrtc
{
	class VideoFrame;
} // namespace webrtc

namespace UE::PixelStreaming 
{
	class FVideoEncoderFactorySimple;
	
	class FVideoEncoderH264Wrapper
	{
	public:
		FVideoEncoderH264Wrapper(TUniquePtr<FEncoderFrameFactory> FrameFactory, TUniquePtr<AVEncoder::FVideoEncoder> Encoder);
		~FVideoEncoderH264Wrapper();

		uint64 GetId() const { return Id; }

		void SetForceNextKeyframe() { bForceNextKeyframe = true; }

		void Encode(const webrtc::VideoFrame& WebRTCFrame, bool bKeyframe);

		AVEncoder::FVideoEncoder::FLayerConfig GetCurrentConfig();
		void SetConfig(const AVEncoder::FVideoEncoder::FLayerConfig& NewConfig);

		static void OnEncodedPacket(FVideoEncoderFactorySimple* Factory, 
			uint32 InLayerIndex, const TSharedPtr<AVEncoder::FVideoEncoderInputFrame> InFrame, 
			const AVEncoder::FCodecPacket& InPacket);

	private:
		uint64 Id;
		TUniquePtr<FEncoderFrameFactory> FrameFactory;
		TUniquePtr<AVEncoder::FVideoEncoder> Encoder;
		bool bForceNextKeyframe = false;
	};
} // namespace UE::PixelStreaming