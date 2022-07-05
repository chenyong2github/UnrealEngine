// Copyright Epic Games, Inc. All Rights Reserved.

#include "VideoEncoderFactorySingleLayer.h"
#include "absl/strings/match.h"
#include "VideoEncoderSingleLayerH264.h"
#include "VideoEncoderSingleLayerVPX.h"
#include "Settings.h"
#include "VideoEncoderFactory.h"
#include "PixelStreamingPrivate.h"
#include "Utils.h"

namespace UE::PixelStreaming
{
	std::vector<webrtc::SdpVideoFormat> FVideoEncoderFactorySingleLayer::GetSupportedFormats() const
	{
		std::vector<webrtc::SdpVideoFormat> video_formats;

		switch (UE::PixelStreaming::Settings::GetSelectedCodec())
		{
			case EPixelStreamingCodec::VP8:
				video_formats.push_back(webrtc::SdpVideoFormat(cricket::kVp8CodecName));
				break;
			case EPixelStreamingCodec::VP9:
				video_formats.push_back(webrtc::SdpVideoFormat(cricket::kVp9CodecName));
				break;
			case EPixelStreamingCodec::H264:
			default:
#if WEBRTC_VERSION == 84
				video_formats.push_back(UE::PixelStreaming::CreateH264Format(webrtc::H264::kProfileConstrainedBaseline, webrtc::H264::kLevel3_1));
				video_formats.push_back(UE::PixelStreaming::CreateH264Format(webrtc::H264::kProfileBaseline, webrtc::H264::kLevel3_1));
#elif WEBRTC_VERSION == 96
				video_formats.push_back(UE::PixelStreaming::CreateH264Format(webrtc::H264Profile::kProfileConstrainedBaseline, webrtc::H264Level::kLevel3_1));
				video_formats.push_back(UE::PixelStreaming::CreateH264Format(webrtc::H264Profile::kProfileBaseline, webrtc::H264Level::kLevel3_1));
#endif
				break;
		}
		return video_formats;
	}

	FVideoEncoderFactorySingleLayer::CodecInfo FVideoEncoderFactorySingleLayer::QueryVideoEncoder(const webrtc::SdpVideoFormat& format) const
	{
		webrtc::VideoEncoderFactory::CodecInfo CodecInfo = { false
#if WEBRTC_VERSION == 84
			,
			false
#endif
		};
#if WEBRTC_VERSION == 84
		CodecInfo.is_hardware_accelerated = true;
#endif
		CodecInfo.has_internal_source = false;
		return CodecInfo;
	}

	std::unique_ptr<webrtc::VideoEncoder> FVideoEncoderFactorySingleLayer::CreateVideoEncoder(const webrtc::SdpVideoFormat& format)
	{
		if (absl::EqualsIgnoreCase(format.name, cricket::kVp8CodecName))
		{
			return std::make_unique<FVideoEncoderSingleLayerVPX>(8);
		}
		else if (absl::EqualsIgnoreCase(format.name, cricket::kVp9CodecName))
		{
			return std::make_unique<FVideoEncoderSingleLayerVPX>(9);
		}
		else
		{
			// Lock during encoder creation
			FScopeLock Lock(&ActiveEncodersGuard);
			auto VideoEncoder = std::make_unique<FVideoEncoderSingleLayerH264>(*this);
			ActiveEncoders.Add(VideoEncoder.get());
			return VideoEncoder;
		}
	}

#if WEBRTC_VERSION == 84
	void FVideoEncoderFactorySingleLayer::OnEncodedImage(const webrtc::EncodedImage& encoded_image, const webrtc::CodecSpecificInfo* codec_specific_info, const webrtc::RTPFragmentationHeader* fragmentation)
#elif WEBRTC_VERSION == 96
	void FVideoEncoderFactorySingleLayer::OnEncodedImage(const webrtc::EncodedImage& encoded_image, const webrtc::CodecSpecificInfo* codec_specific_info)
#endif
	{
		// Lock as we send encoded image to each encoder.
		FScopeLock Lock(&ActiveEncodersGuard);

		// Go through each encoder and send our encoded image to its callback
		for (FVideoEncoderSingleLayerH264* Encoder : ActiveEncoders)
		{
			Encoder->SendEncodedImage(encoded_image, codec_specific_info
#if WEBRTC_VERSION == 84
				,
				fragmentation
#endif
			);
		}
	}

	void FVideoEncoderFactorySingleLayer::ReleaseVideoEncoder(FVideoEncoderSingleLayerH264* Encoder)
	{
		// Lock during deleting an encoder
		FScopeLock Lock(&ActiveEncodersGuard);
		ActiveEncoders.Remove(Encoder);
	}

	FVideoEncoderWrapperHardware* FVideoEncoderFactorySingleLayer::GetOrCreateHardwareEncoder(int Width, int Height, int MaxBitrate, int TargetBitrate, int MaxFramerate)
	{
		if (HardwareEncoder == nullptr)
		{
			// Make AVEncoder frame factory.
			TUniquePtr<FEncoderFrameFactory> FrameFactory = MakeUnique<FEncoderFrameFactory>();

			// Make the encoder config
			AVEncoder::FVideoEncoder::FLayerConfig EncoderConfig;
			EncoderConfig.Width = Width;
			EncoderConfig.Height = Height;
			EncoderConfig.MaxFramerate = MaxFramerate;
			EncoderConfig.TargetBitrate = TargetBitrate;
			EncoderConfig.MaxBitrate = MaxBitrate;

			// Make the actual AVEncoder encoder.
			const TArray<AVEncoder::FVideoEncoderInfo>& Available = AVEncoder::FVideoEncoderFactory::Get().GetAvailable();
			TUniquePtr<AVEncoder::FVideoEncoder> Encoder = AVEncoder::FVideoEncoderFactory::Get().Create(Available[0].ID, FrameFactory->GetOrCreateVideoEncoderInput(), EncoderConfig);
			if (Encoder.IsValid())
			{
				Encoder->SetOnEncodedPacket([this](uint32 InLayerIndex, const TSharedPtr<AVEncoder::FVideoEncoderInputFrame> InFrame, const AVEncoder::FCodecPacket& InPacket) {
					// Note: this is a static method call.
					FVideoEncoderWrapperHardware::OnEncodedPacket(this, InLayerIndex, InFrame, InPacket);
				});

				// Make the hardware encoder wrapper
				HardwareEncoder = MakeUnique<FVideoEncoderWrapperHardware>(MoveTemp(FrameFactory), MoveTemp(Encoder));
				return HardwareEncoder.Get();
			}
			else
			{
				UE_LOG(LogPixelStreaming, Error, TEXT("Could not create encoder. Check encoder config or perhaps you used up all your HW encoders."));
				// We could not make the encoder, so indicate the id was not set successfully.
				return nullptr;
			}
		}
		else
		{
			return HardwareEncoder.Get();
		}
	}

	FVideoEncoderWrapperHardware* FVideoEncoderFactorySingleLayer::GetHardwareEncoder()
	{
		return HardwareEncoder.Get();
	}

	void FVideoEncoderFactorySingleLayer::ForceKeyFrame()
	{
		if (HardwareEncoder)
		{
			HardwareEncoder->SetForceNextKeyframe();
		}
	}
} // namespace UE::PixelStreaming
