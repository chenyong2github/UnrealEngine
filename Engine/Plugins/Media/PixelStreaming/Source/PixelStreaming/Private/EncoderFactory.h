// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "WebRTCIncludes.h"
#include "VideoEncoder.h"
#include "Containers/Queue.h"
#include "PixelStreamingPlayerId.h"
#include "HAL/CriticalSection.h"
#include "Misc/Optional.h"

namespace UE {
	namespace PixelStreaming {
		class FVideoEncoderFactory;
		class FVideoEncoderRTC;
		class FVideoEncoderH264Wrapper;

		class FSimulcastEncoderFactory : public webrtc::VideoEncoderFactory
		{
		public:
			FSimulcastEncoderFactory();
			virtual ~FSimulcastEncoderFactory();
			virtual std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const ;
			virtual CodecInfo QueryVideoEncoder(const webrtc::SdpVideoFormat& format) const override;
			virtual std::unique_ptr<webrtc::VideoEncoder> CreateVideoEncoder(const webrtc::SdpVideoFormat& format);


			using FEncoderFactoryId = uint64;
			FVideoEncoderFactory* GetOrCreateEncoderFactory(FEncoderFactoryId Id);
			FVideoEncoderFactory* GetEncoderFactory(FEncoderFactoryId Id);
		private:
			TMap<FEncoderFactoryId, TUniquePtr<FVideoEncoderFactory>> EncoderFactories;
			FCriticalSection EncoderFactoriesGuard;

			TUniquePtr<FVideoEncoderFactory> PrimaryEncoderFactory;
		};

		class FVideoEncoderFactory : public webrtc::VideoEncoderFactory
		{
		public:
			FVideoEncoderFactory();
			virtual ~FVideoEncoderFactory() override;

			virtual std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override;

			// Always returns our H264 hardware encoders codec_info for now/
			virtual CodecInfo QueryVideoEncoder(const webrtc::SdpVideoFormat& Format) const override;

			// Always returns our H264 hardware encoder for now.
			virtual std::unique_ptr<webrtc::VideoEncoder> CreateVideoEncoder(const webrtc::SdpVideoFormat& Format) override;

			void ReleaseVideoEncoder(FVideoEncoderRTC* Encoder);
			void ForceKeyFrame();

			
			FVideoEncoderH264Wrapper* GetOrCreateHardwareEncoder(int Width, int Height, int MaxBitrate, int TargetBitrate, int MaxFramerate);
			FVideoEncoderH264Wrapper* GetHardwareEncoder();

			void OnEncodedImage(const webrtc::EncodedImage& Encoded_image, const webrtc::CodecSpecificInfo* CodecSpecificInfo, const webrtc::RTPFragmentationHeader* Fragmentation);

		private:

			static webrtc::SdpVideoFormat CreateH264Format(webrtc::H264::Profile Profile, webrtc::H264::Level Level);

			TUniquePtr<FVideoEncoderH264Wrapper> HardwareEncoder;

			// Encoders assigned to each peer. Each one of these will be assigned to one of the hardware encoders
			TArray<FVideoEncoderRTC*> ActiveEncoders;
			FCriticalSection ActiveEncodersGuard;
		};
	}
}
