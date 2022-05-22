// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "WebRTCIncludes.h"

namespace UE::PixelStreaming 
{
	class FVideoEncoderRTC;
	class FVideoEncoderH264Wrapper;
	
	class FVideoEncoderFactorySimple : public webrtc::VideoEncoderFactory
	{
	public:
		FVideoEncoderFactorySimple();
		virtual ~FVideoEncoderFactorySimple() override;

		virtual std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override;

		// Always returns our H264 hardware encoders codec_info for now/
		virtual CodecInfo QueryVideoEncoder(const webrtc::SdpVideoFormat& Format) const override;

		// Always returns our H264 hardware encoder for now.
		virtual std::unique_ptr<webrtc::VideoEncoder> CreateVideoEncoder(const webrtc::SdpVideoFormat& Format) override;

		void ReleaseVideoEncoder(FVideoEncoderRTC* Encoder);
		void ForceKeyFrame();
			
		FVideoEncoderH264Wrapper* GetOrCreateHardwareEncoder(int Width, int Height, int MaxBitrate, int TargetBitrate, int MaxFramerate);
		FVideoEncoderH264Wrapper* GetHardwareEncoder();

#if WEBRTC_VERSION == 84
		void OnEncodedImage(const webrtc::EncodedImage& Encoded_image, const webrtc::CodecSpecificInfo* CodecSpecificInfo, const webrtc::RTPFragmentationHeader* Fragmentation);
#elif WEBRTC_VERSION == 96
		void OnEncodedImage(const webrtc::EncodedImage& Encoded_image, const webrtc::CodecSpecificInfo* CodecSpecificInfo);
#endif

	private:
#if WEBRTC_VERSION == 84
		static webrtc::SdpVideoFormat CreateH264Format(webrtc::H264::Profile Profile, webrtc::H264::Level Level);
#elif WEBRTC_VERSION == 96
		static webrtc::SdpVideoFormat CreateH264Format(webrtc::H264Profile Profile, webrtc::H264Level Level);
#endif

		TUniquePtr<FVideoEncoderH264Wrapper> HardwareEncoder;

		// Encoders assigned to each peer. Each one of these will be assigned to one of the hardware encoders
		// raw pointers here because the factory interface wants a unique_ptr here so we cant own the object.
		TArray<FVideoEncoderRTC*> ActiveEncoders;
		FCriticalSection ActiveEncodersGuard;
	};
} // namespace UE::PixelStreaming
