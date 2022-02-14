// Copyright Epic Games, Inc. All Rights Reserved.

#include "EncoderFactory.h"
#include "VideoEncoderRTC.h"
#include "Settings.h"
#include "absl/strings/match.h"
#include "modules/video_coding/codecs/vp8/include/vp8.h"
#include "modules/video_coding/codecs/vp9/include/vp9.h"
#include "Misc/ScopeLock.h"
#include "VideoEncoderH264Wrapper.h"
#include "SimulcastEncoderAdapter.h"
#include "VideoEncoderFactory.h"
#include "PixelStreamingPrivate.h"
#include "Utils.h"

/*
* ------------- UE::PixelStreaming::FVideoEncoderFactory ---------------
*/

UE::PixelStreaming::FVideoEncoderFactory::FVideoEncoderFactory()
{
}

UE::PixelStreaming::FVideoEncoderFactory::~FVideoEncoderFactory()
{
}

std::vector<webrtc::SdpVideoFormat> UE::PixelStreaming::FVideoEncoderFactory::GetSupportedFormats() const
{
	std::vector<webrtc::SdpVideoFormat> video_formats;

	switch (UE::PixelStreaming::Settings::GetSelectedCodec())
	{
		case UE::PixelStreaming::Settings::ECodec::VP8:
			video_formats.push_back(webrtc::SdpVideoFormat(cricket::kVp8CodecName));
		case UE::PixelStreaming::Settings::ECodec::VP9:
			video_formats.push_back(webrtc::SdpVideoFormat(cricket::kVp9CodecName));
		case UE::PixelStreaming::Settings::ECodec::H264:
		default:
			video_formats.push_back(UE::PixelStreaming::CreateH264Format(webrtc::H264::kProfileConstrainedBaseline, webrtc::H264::kLevel3_1));
			video_formats.push_back(UE::PixelStreaming::CreateH264Format(webrtc::H264::kProfileBaseline, webrtc::H264::kLevel3_1));
	}
	return video_formats;
}

UE::PixelStreaming::FVideoEncoderFactory::CodecInfo UE::PixelStreaming::FVideoEncoderFactory::QueryVideoEncoder(const webrtc::SdpVideoFormat& format) const
{
	webrtc::VideoEncoderFactory::CodecInfo CodecInfo = { false, false };
	CodecInfo.is_hardware_accelerated = true;
	CodecInfo.has_internal_source = false;
	return CodecInfo;
}

std::unique_ptr<webrtc::VideoEncoder> UE::PixelStreaming::FVideoEncoderFactory::CreateVideoEncoder(const webrtc::SdpVideoFormat& format)
{
	if (absl::EqualsIgnoreCase(format.name, cricket::kVp8CodecName))
	{
		return webrtc::VP8Encoder::Create();
	}
	else if (absl::EqualsIgnoreCase(format.name, cricket::kVp9CodecName))
	{
		return webrtc::VP9Encoder::Create();
	}
	else
	{
		// Lock during encoder creation
		FScopeLock Lock(&ActiveEncodersGuard);
		auto VideoEncoder = std::make_unique<UE::PixelStreaming::FVideoEncoderRTC>(*this);
		ActiveEncoders.Add(VideoEncoder.get());
		return VideoEncoder;
	}
}

void UE::PixelStreaming::FVideoEncoderFactory::OnEncodedImage(const webrtc::EncodedImage& encoded_image, const webrtc::CodecSpecificInfo* codec_specific_info, const webrtc::RTPFragmentationHeader* fragmentation)
{
	// Lock as we send encoded image to each encoder.
	FScopeLock Lock(&ActiveEncodersGuard);

	// Go through each encoder and send our encoded image to its callback
	for (UE::PixelStreaming::FVideoEncoderRTC* Encoder : ActiveEncoders)
	{
		Encoder->SendEncodedImage(encoded_image, codec_specific_info, fragmentation);
	}
}

void UE::PixelStreaming::FVideoEncoderFactory::ReleaseVideoEncoder(UE::PixelStreaming::FVideoEncoderRTC* Encoder)
{
	// Lock during deleting an encoder
	FScopeLock Lock(&ActiveEncodersGuard);
	ActiveEncoders.Remove(Encoder);
}

void UE::PixelStreaming::FVideoEncoderFactory::ForceKeyFrame()
{
	FScopeLock Lock(&ActiveEncodersGuard);
	HardwareEncoder->SetForceNextKeyframe();
}

UE::PixelStreaming::FVideoEncoderH264Wrapper* UE::PixelStreaming::FVideoEncoderFactory::GetOrCreateHardwareEncoder(int Width, int Height, int MaxBitrate, int TargetBitrate, int MaxFramerate)
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

UE::PixelStreaming::FVideoEncoderH264Wrapper* UE::PixelStreaming::FVideoEncoderFactory::GetHardwareEncoder()
{
	return HardwareEncoder.Get();
}

/*
* ------------- UE::PixelStreaming::FSimulcastEncoderFactory ---------------
*/

UE::PixelStreaming::FSimulcastEncoderFactory::FSimulcastEncoderFactory()
	: PrimaryEncoderFactory(MakeUnique<UE::PixelStreaming::FVideoEncoderFactory>())
{
	// Make a copy of simulcast settings and sort them based on scaling.
	for (FEncoderFactoryId i = 0; i < Settings::SimulcastParameters.Layers.Num(); i++)
	{
		GetOrCreateEncoderFactory(i);
	}
}

UE::PixelStreaming::FSimulcastEncoderFactory::~FSimulcastEncoderFactory()
{
}

std::unique_ptr<webrtc::VideoEncoder> UE::PixelStreaming::FSimulcastEncoderFactory::CreateVideoEncoder(const webrtc::SdpVideoFormat& format)
{
	return std::make_unique<UE::PixelStreaming::FSimulcastEncoderAdapter>(*this, format);
}

UE::PixelStreaming::FVideoEncoderFactory* UE::PixelStreaming::FSimulcastEncoderFactory::GetEncoderFactory(FEncoderFactoryId Id)
{
	FScopeLock Lock(&EncoderFactoriesGuard);
	if (auto&& Existing = EncoderFactories.Find(Id))
	{
		return Existing->Get();
	}
	return nullptr;
}

UE::PixelStreaming::FVideoEncoderFactory* UE::PixelStreaming::FSimulcastEncoderFactory::GetOrCreateEncoderFactory(FEncoderFactoryId Id)
{
	UE::PixelStreaming::FVideoEncoderFactory* Existing = GetEncoderFactory(Id);
	if (Existing == nullptr)
	{
		FScopeLock Lock(&EncoderFactoriesGuard);
		TUniquePtr<FVideoEncoderFactory> EncoderFactory = MakeUnique<UE::PixelStreaming::FVideoEncoderFactory>();
		EncoderFactories.Add(Id, MoveTemp(EncoderFactory));
		return EncoderFactory.Get();
	}
	else
	{
		return Existing;
	}
}

std::vector<webrtc::SdpVideoFormat> UE::PixelStreaming::FSimulcastEncoderFactory::GetSupportedFormats() const
{
	return PrimaryEncoderFactory->GetSupportedFormats();
}

UE::PixelStreaming::FSimulcastEncoderFactory::CodecInfo UE::PixelStreaming::FSimulcastEncoderFactory::QueryVideoEncoder(const webrtc::SdpVideoFormat& format) const
{
	return PrimaryEncoderFactory->QueryVideoEncoder(format);
}
