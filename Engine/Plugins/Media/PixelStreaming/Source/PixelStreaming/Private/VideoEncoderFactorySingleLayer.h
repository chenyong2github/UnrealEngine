// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "WebRTCIncludes.h"

#include "Video/VideoEncoder.h"
#include "Video/Encoders/Configs/VideoEncoderConfigH264.h"
#include "Video/Encoders/Configs/VideoEncoderConfigH265.h"
#include "Video/Resources/VideoResourceRHI.h"

namespace UE::PixelStreaming
{
	using FVideoEncoderHardware = TVideoEncoder<FVideoResourceRHI>;

	class FVideoEncoderSingleLayerHardware;

	/**
	 * An encoder factory for a single layer. Do not use this directly. Use FVideoEncoderFactoryLayered even
	 * when not using simulcast specifically.
	 */
	class PIXELSTREAMING_API FVideoEncoderFactorySingleLayer : public webrtc::VideoEncoderFactory
	{
	public:
		FVideoEncoderFactorySingleLayer() = default;
		virtual ~FVideoEncoderFactorySingleLayer() override = default;

		virtual std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override;

		// Always returns our H264 hardware encoders codec_info for now/
		virtual CodecInfo QueryVideoEncoder(const webrtc::SdpVideoFormat& Format) const override;

		// Always returns our H264 hardware encoder for now.
		virtual std::unique_ptr<webrtc::VideoEncoder> CreateVideoEncoder(const webrtc::SdpVideoFormat& Format) override;

		void ReleaseVideoEncoder(FVideoEncoderSingleLayerHardware* Encoder);

		TWeakPtr<FVideoEncoderHardware> GetHardwareEncoder() const;
		TWeakPtr<FVideoEncoderHardware> GetOrCreateHardwareEncoder(const FVideoEncoderConfigH264& VideoConfig);
		TWeakPtr<FVideoEncoderHardware> GetOrCreateHardwareEncoder(const FVideoEncoderConfigH265& VideoConfig);

		void ForceKeyFrame();
		bool ShouldForceKeyframe() const;
		void UnforceKeyFrame();
		void OnEncodedImage(const webrtc::EncodedImage& Encoded_image, const webrtc::CodecSpecificInfo* CodecSpecificInfo);

	private:
		static webrtc::SdpVideoFormat CreateH264Format(webrtc::H264Profile Profile, webrtc::H264Level Level);

		TSharedPtr<FVideoEncoderHardware> HardwareEncoder;

		uint8 bForceNextKeyframe : 1;

		// Encoders assigned to each peer. Each one of these will be assigned to one of the hardware encoders
		// raw pointers here because the factory interface wants a unique_ptr here so we cant own the object.
		TArray<FVideoEncoderSingleLayerHardware*> ActiveEncoders;
		FCriticalSection ActiveEncodersGuard;

		// Init encoder guard
		FCriticalSection InitEncoderGuard;
	};
} // namespace UE::PixelStreaming
