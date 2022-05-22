// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "WebRTCIncludes.h"

namespace UE::PixelStreaming 
{
	class FVideoEncoderFactorySimple;
	
	class FVideoEncoderFactorySimulcast : public webrtc::VideoEncoderFactory
	{
	public:
		FVideoEncoderFactorySimulcast();
		virtual ~FVideoEncoderFactorySimulcast();
		virtual std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const ;
		virtual CodecInfo QueryVideoEncoder(const webrtc::SdpVideoFormat& format) const override;
		virtual std::unique_ptr<webrtc::VideoEncoder> CreateVideoEncoder(const webrtc::SdpVideoFormat& format);

		using FEncoderFactoryId = uint64;
		FVideoEncoderFactorySimple* GetOrCreateEncoderFactory(FEncoderFactoryId Id);
		FVideoEncoderFactorySimple* GetEncoderFactory(FEncoderFactoryId Id);
	private:
		TMap<FEncoderFactoryId, TUniquePtr<FVideoEncoderFactorySimple>> EncoderFactories;
		FCriticalSection EncoderFactoriesGuard;

		TUniquePtr<FVideoEncoderFactorySimple> PrimaryEncoderFactory;
	};
} // namespace UE::PixelStreaming
