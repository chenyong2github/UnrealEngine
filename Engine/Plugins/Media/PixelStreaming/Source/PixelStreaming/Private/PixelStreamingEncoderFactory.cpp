// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingEncoderFactory.h"
#include "PixelStreamingVideoEncoder.h"
#include "PixelStreamingSettings.h"
#include "Utils.h"
#include "absl/strings/match.h"
#include "modules/video_coding/codecs/vp8/include/vp8.h"
#include "Misc/ScopeLock.h"

FPixelStreamingVideoEncoderFactory::FPixelStreamingVideoEncoderFactory(IPixelStreamingSessions* InPixelStreamingSessions)
	: PixelStreamingSessions(InPixelStreamingSessions)
{
	EncoderContext.Factory = this;
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
		FScopeLock FactoryLock(&this->FactoryCS);
		auto VideoEncoder = std::make_unique<FPixelStreamingVideoEncoder>(this->PixelStreamingSessions, &EncoderContext);
		this->ActiveEncoders.Add(VideoEncoder.get());
		
		UE_LOG(PixelStreamer, Log, TEXT("Encoder factory addded new encoder - soon to be associated with a player."));
		return VideoEncoder;
	}
}

void FPixelStreamingVideoEncoderFactory::RemoveStaleEncoders()
{
	// Lock during removing stale encoders
	FScopeLock FactoryLock(&this->FactoryCS);

	// Iterate backwards so we can remove invalid encoders along the way
	for (int32 Index = ActiveEncoders.Num()-1; Index >= 0; --Index)
	{
		FPixelStreamingVideoEncoder* Encoder = ActiveEncoders[Index];
		if(!Encoder->IsRegisteredWithWebRTC())
		{
			ActiveEncoders.RemoveAt(Index);
			UE_LOG(PixelStreamer, Log, TEXT("Encoder factory cleaned up stale encoder associated with PlayerId=%s"), *Encoder->GetPlayerId());
		}
	}
}

void FPixelStreamingVideoEncoderFactory::OnEncodedImage(const webrtc::EncodedImage& encoded_image, const webrtc::CodecSpecificInfo* codec_specific_info, const webrtc::RTPFragmentationHeader* fragmentation)
{
	// Before sending encoded image to each encoder's callback, check if all encoders we have are still relevant.
	this->RemoveStaleEncoders();

	// Lock as we send encoded image to each encoder.
	FScopeLock FactoryLock(&this->FactoryCS);

	// Go through each encoder and send our encoded image to its callback
	for(FPixelStreamingVideoEncoder* Encoder : ActiveEncoders)
	{
		if(Encoder->IsRegisteredWithWebRTC())
		{
			Encoder->SendEncodedImage(encoded_image, codec_specific_info, fragmentation);
		}
	}

	// Store the QP of this encoded image as we send the smoothed value to the peers as a proxy for encoding quality
	EncoderContext.SmoothedAvgQP.Update(encoded_image.qp_);
}

void FPixelStreamingVideoEncoderFactory::ReleaseVideoEncoder(FPixelStreamingVideoEncoder* encoder)
{
	// Lock during deleting an encoder
	FScopeLock FactoryLock(&this->FactoryCS);

	ActiveEncoders.Remove(encoder);
	if (ActiveEncoders.Num() == 0 && EncoderContext.Encoder != nullptr)
	{
		EncoderContext.Encoder->ClearOnEncodedPacket();
		EncoderContext.Encoder->Shutdown();
		EncoderContext.Encoder = nullptr;
	}
	FPlayerId PlayerId = encoder->GetPlayerId();
	UE_LOG(PixelStreamer, Log, TEXT("Encoder factory asked to remove encoder for PlayerId=%s"), *PlayerId);
}

void FPixelStreamingVideoEncoderFactory::ForceKeyFrame()
{
	FScopeLock FactoryLock(&this->FactoryCS);
	// Go through each encoder and send our encoded image to its callback
	for(FPixelStreamingVideoEncoder* Encoder : ActiveEncoders)
	{
		if(Encoder->IsRegisteredWithWebRTC())
		{
			Encoder->ForceKeyFrame();
		}
	}
}

double FPixelStreamingVideoEncoderFactory::GetLatestQP()
{
	return this->EncoderContext.SmoothedAvgQP.Get();
}