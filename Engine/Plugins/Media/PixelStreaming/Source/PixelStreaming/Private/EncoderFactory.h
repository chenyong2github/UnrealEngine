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
			virtual std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const;
			virtual CodecInfo QueryVideoEncoder(const webrtc::SdpVideoFormat& format) const override;
			virtual std::unique_ptr<webrtc::VideoEncoder> CreateVideoEncoder(const webrtc::SdpVideoFormat& format);

			FVideoEncoderFactory* GetRealFactory() const;

		private:
			// making this a unique ptr is a little cheeky since we pass around raw pointers to it
			// but the sole ownership lies in this class
			TUniquePtr<FVideoEncoderFactory> RealFactory;
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

			using FHardwareEncoderId = uint64;
			TOptional<FHardwareEncoderId> GetOrCreateHardwareEncoder(int Width, int Height, int MaxBitrate, int TargetBitrate, int MaxFramerate);
			FVideoEncoderH264Wrapper* GetHardwareEncoder(FHardwareEncoderId Id);

			void OnEncodedImage(FHardwareEncoderId SourceEncoderId, const webrtc::EncodedImage& Encoded_image, const webrtc::CodecSpecificInfo* CodecSpecificInfo, const webrtc::RTPFragmentationHeader* Fragmentation);

		private:

			static webrtc::SdpVideoFormat CreateH264Format(webrtc::H264::Profile Profile, webrtc::H264::Level Level);

			// These are the actual hardware encoders serving multiple pixelstreaming encoders
			TMap<FHardwareEncoderId, TUniquePtr<FVideoEncoderH264Wrapper>> HardwareEncoders;
			FCriticalSection HardwareEncodersGuard;

			// Encoders assigned to each peer. Each one of these will be assigned to one of the hardware encoders
			TArray<FVideoEncoderRTC*> ActiveEncoders;
			FCriticalSection ActiveEncodersGuard;
		};
	}
}
