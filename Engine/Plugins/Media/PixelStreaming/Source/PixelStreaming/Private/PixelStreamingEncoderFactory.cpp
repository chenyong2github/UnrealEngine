// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingEncoderFactory.h"
#include "PixelStreamingVideoEncoder.h"
#include "PixelStreamingSettings.h"
#include "Utils.h"
#include "absl/strings/match.h"
#include "modules/video_coding/codecs/vp8/include/vp8.h"
#include "Misc/ScopeLock.h"
#include "PixelStreamingRealEncoder.h"

#include "PixelStreamingSimulcastEncoderAdapter.h"

FPixelStreamingSimulcastEncoderFactory::FPixelStreamingSimulcastEncoderFactory(IPixelStreamingSessions* InPixelStreamingSessions)
	: RealFactory(new FPixelStreamingVideoEncoderFactory(InPixelStreamingSessions))
{
}

FPixelStreamingSimulcastEncoderFactory::~FPixelStreamingSimulcastEncoderFactory()
{
}

std::vector<webrtc::SdpVideoFormat> FPixelStreamingSimulcastEncoderFactory::GetSupportedFormats() const
{
	return RealFactory->GetSupportedFormats();
}

FPixelStreamingSimulcastEncoderFactory::CodecInfo FPixelStreamingSimulcastEncoderFactory::QueryVideoEncoder(const webrtc::SdpVideoFormat& format) const
{
	return RealFactory->QueryVideoEncoder(format);
}

std::unique_ptr<webrtc::VideoEncoder> FPixelStreamingSimulcastEncoderFactory::CreateVideoEncoder(const webrtc::SdpVideoFormat& format)
{
	return std::make_unique<PixelStreamingSimulcastEncoderAdapter>(RealFactory.Get(), format);
}

FPixelStreamingVideoEncoderFactory* FPixelStreamingSimulcastEncoderFactory::GetRealFactory() const
{
	return RealFactory.Get();
}

FPixelStreamingVideoEncoderFactory::FPixelStreamingVideoEncoderFactory(IPixelStreamingSessions* InPixelStreamingSessions)
	: PixelStreamingSessions(InPixelStreamingSessions)
{
}

FPixelStreamingVideoEncoderFactory::~FPixelStreamingVideoEncoderFactory()
{
}

std::vector<webrtc::SdpVideoFormat> FPixelStreamingVideoEncoderFactory::GetSupportedFormats() const
{
	const bool bForceVP8 = PixelStreamingSettings::IsForceVP8();

	std::vector<webrtc::SdpVideoFormat> video_formats;
	if (bForceVP8)
	{
		video_formats.push_back(webrtc::SdpVideoFormat(cricket::kVp8CodecName));
	}
	else
	{
		video_formats.push_back(CreateH264Format(webrtc::H264::kProfileConstrainedBaseline, webrtc::H264::kLevel3_1));
	}

	return video_formats;
}

FPixelStreamingVideoEncoderFactory::CodecInfo FPixelStreamingVideoEncoderFactory::QueryVideoEncoder(const webrtc::SdpVideoFormat& format) const
{
	CodecInfo codec_info = { false, false };
	codec_info.is_hardware_accelerated = true;
	codec_info.has_internal_source = false;
	return codec_info;
}

std::unique_ptr<webrtc::VideoEncoder> FPixelStreamingVideoEncoderFactory::CreateVideoEncoder(const webrtc::SdpVideoFormat& format)
{
	if (absl::EqualsIgnoreCase(format.name, cricket::kVp8CodecName))
		return webrtc::VP8Encoder::Create();
	else
	{
		// Lock during encoder creation
		FScopeLock Lock(&ActiveEncodersGuard);
		auto VideoEncoder = std::make_unique<FPixelStreamingVideoEncoder>(PixelStreamingSessions, *this);
		ActiveEncoders.Add(VideoEncoder.get());
		return VideoEncoder;
	}
}

void FPixelStreamingVideoEncoderFactory::OnEncodedImage(FHardwareEncoderId SourceEncoderId, const webrtc::EncodedImage& encoded_image, const webrtc::CodecSpecificInfo* codec_specific_info, const webrtc::RTPFragmentationHeader* fragmentation)
{
	// Lock as we send encoded image to each encoder.
	FScopeLock Lock(&ActiveEncodersGuard);

	// Go through each encoder and send our encoded image to its callback
	for (FPixelStreamingVideoEncoder* Encoder : ActiveEncoders)
	{
		Encoder->SendEncodedImage(SourceEncoderId, encoded_image, codec_specific_info, fragmentation);
	}
}

void FPixelStreamingVideoEncoderFactory::ReleaseVideoEncoder(FPixelStreamingVideoEncoder* encoder)
{
	// Lock during deleting an encoder
	FScopeLock Lock(&ActiveEncodersGuard);
	ActiveEncoders.Remove(encoder);
}

void FPixelStreamingVideoEncoderFactory::ForceKeyFrame()
{
	FScopeLock Lock(&ActiveEncodersGuard);
	// Go through each encoder and send our encoded image to its callback
	for (auto& KeyAndEncoder : HardwareEncoders)
	{
		KeyAndEncoder.Value->SetForceNextKeyframe();
	}
}

namespace
{
	uint64 HashResolution(uint32 Width, uint32 Height)
	{
		return static_cast<uint64>(Width) << 32 | static_cast<uint64>(Height);
	}
} // namespace

FPixelStreamingVideoEncoderFactory::FHardwareEncoderId FPixelStreamingVideoEncoderFactory::GetOrCreateHardwareEncoder(int Width, int Height, int MaxBitrate, int TargetBitrate, int MaxFramerate)
{
	FHardwareEncoderId EncoderId = HashResolution(Width, Height);
	FPixelStreamingRealEncoder* Existing = GetHardwareEncoder(EncoderId);

	if (Existing == nullptr)
	{
		TUniquePtr<FPixelStreamingRealEncoder> Encoder = MakeUnique<FPixelStreamingRealEncoder>(EncoderId, Width, Height, MaxBitrate, TargetBitrate, MaxFramerate, this);
		FPixelStreamingRealEncoder* ReturnValue = Encoder.Get();
		{
			FScopeLock Lock(&HardwareEncodersGuard);
			HardwareEncoders.Add(EncoderId, MoveTemp(Encoder));
		}
	}

	return EncoderId;
}

FPixelStreamingRealEncoder* FPixelStreamingVideoEncoderFactory::GetHardwareEncoder(FHardwareEncoderId Id)
{
	FScopeLock Lock(&HardwareEncodersGuard);
	if (auto&& Existing = HardwareEncoders.Find(Id))
	{
		return Existing->Get();
	}
	return nullptr;
}
