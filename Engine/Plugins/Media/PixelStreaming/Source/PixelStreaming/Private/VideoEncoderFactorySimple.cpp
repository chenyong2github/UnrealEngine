// Copyright Epic Games, Inc. All Rights Reserved.

#include "VideoEncoderFactorySimple.h"
#include "absl/strings/match.h"
#include "VideoEncoderRTC.h"
#include "VideoEncoderVPX.h"
#include "Settings.h"
#include "VideoEncoderFactory.h"
#include "PixelStreamingPrivate.h"
#include "Utils.h"

namespace UE::PixelStreaming
{
	FVideoEncoderFactorySimple::FVideoEncoderFactorySimple()
	{
	}

	FVideoEncoderFactorySimple::~FVideoEncoderFactorySimple()
	{
	}

	std::vector<webrtc::SdpVideoFormat> FVideoEncoderFactorySimple::GetSupportedFormats() const
	{
		std::vector<webrtc::SdpVideoFormat> video_formats;

		switch (UE::PixelStreaming::Settings::GetSelectedCodec())
		{
			case UE::PixelStreaming::Settings::ECodec::VP8:
				video_formats.push_back(webrtc::SdpVideoFormat(cricket::kVp8CodecName));
				break;
			case UE::PixelStreaming::Settings::ECodec::VP9:
				video_formats.push_back(webrtc::SdpVideoFormat(cricket::kVp9CodecName));
				break;
			case UE::PixelStreaming::Settings::ECodec::H264:
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

	FVideoEncoderFactorySimple::CodecInfo FVideoEncoderFactorySimple::QueryVideoEncoder(const webrtc::SdpVideoFormat& format) const
	{
		webrtc::VideoEncoderFactory::CodecInfo CodecInfo = { false
#if WEBRTC_VERSION == 84
			, false 
#endif
		};
#if WEBRTC_VERSION == 84
		CodecInfo.is_hardware_accelerated = true;
#endif
		CodecInfo.has_internal_source = false;
		return CodecInfo;
	}

	std::unique_ptr<webrtc::VideoEncoder> FVideoEncoderFactorySimple::CreateVideoEncoder(const webrtc::SdpVideoFormat& format)
	{
		if (absl::EqualsIgnoreCase(format.name, cricket::kVp8CodecName))
		{
			return std::make_unique<FVideoEncoderVPX>(8);
		}
		else if (absl::EqualsIgnoreCase(format.name, cricket::kVp9CodecName))
		{
			return std::make_unique<FVideoEncoderVPX>(9);
		}
		else
		{
			// Lock during encoder creation
			FScopeLock Lock(&ActiveEncodersGuard);
			auto VideoEncoder = std::make_unique<FVideoEncoderRTC>(*this);
			ActiveEncoders.Add(VideoEncoder.get());
			return VideoEncoder;
		}
	}

#if WEBRTC_VERSION == 84
	void FVideoEncoderFactorySimple::OnEncodedImage(const webrtc::EncodedImage& encoded_image, const webrtc::CodecSpecificInfo* codec_specific_info, const webrtc::RTPFragmentationHeader* fragmentation)
#elif WEBRTC_VERSION == 96
	void FVideoEncoderFactorySimple::OnEncodedImage(const webrtc::EncodedImage& encoded_image, const webrtc::CodecSpecificInfo* codec_specific_info)
#endif
	{
		// Lock as we send encoded image to each encoder.
		FScopeLock Lock(&ActiveEncodersGuard);

		// Go through each encoder and send our encoded image to its callback
		for (FVideoEncoderRTC* Encoder : ActiveEncoders)
		{
			Encoder->SendEncodedImage(encoded_image, codec_specific_info
#if WEBRTC_VERSION == 84
				, fragmentation
#endif
			);
		}
	}

	void FVideoEncoderFactorySimple::ReleaseVideoEncoder(FVideoEncoderRTC* Encoder)
	{
		// Lock during deleting an encoder
		FScopeLock Lock(&ActiveEncodersGuard);
		ActiveEncoders.Remove(Encoder);
	}

	void FVideoEncoderFactorySimple::ForceKeyFrame()
	{
		FScopeLock Lock(&ActiveEncodersGuard);
		HardwareEncoder->SetForceNextKeyframe();
	}

	FVideoEncoderH264Wrapper* FVideoEncoderFactorySimple::GetOrCreateHardwareEncoder(int Width, int Height, int MaxBitrate, int TargetBitrate, int MaxFramerate)
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
					FVideoEncoderH264Wrapper::OnEncodedPacket(this, InLayerIndex, InFrame, InPacket);
				});

				// Make the hardware encoder wrapper
				HardwareEncoder = MakeUnique<FVideoEncoderH264Wrapper>(MoveTemp(FrameFactory), MoveTemp(Encoder));
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

	FVideoEncoderH264Wrapper* FVideoEncoderFactorySimple::GetHardwareEncoder()
	{
		return HardwareEncoder.Get();
	}
} // namespace UE::PixelStreaming
