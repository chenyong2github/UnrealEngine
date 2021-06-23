// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingEncoderFactory.h"
#include "PlayerSession.h"
#include "PixelStreamingVideoEncoder.h"
#include "PixelStreamingSettings.h"
#include "Utils.h"
#include "absl/strings/match.h"
#include "modules/video_coding/codecs/vp8/include/vp8.h"

FPixelStreamingVideoEncoderFactory::FPixelStreamingVideoEncoderFactory()
{
	EncoderContext.Factory = this;
}

FPixelStreamingVideoEncoderFactory::~FPixelStreamingVideoEncoderFactory()
{
}

void FPixelStreamingVideoEncoderFactory::AddSession(FPlayerSession& PlayerSession)
{
	PendingPlayerSessions.Enqueue(&PlayerSession);
}

std::vector<webrtc::SdpVideoFormat> FPixelStreamingVideoEncoderFactory::GetSupportedFormats() const
{
	const bool bForceVP8 = PixelStreamingSettings::IsForceVP8();

	std::vector<webrtc::SdpVideoFormat> video_formats;
	if (bForceVP8)
		video_formats.push_back(webrtc::SdpVideoFormat(cricket::kVp8CodecName));
	else
		video_formats.push_back(CreateH264Format(webrtc::H264::kProfileConstrainedBaseline, webrtc::H264::kLevel3_1));
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
		FPlayerSession* Session;
		bool res = PendingPlayerSessions.Dequeue(Session);
		checkf(res, TEXT("no player session associated with encoder instance"));

		UE_LOG(PixelStreamer, Log, TEXT("Encoder factory addded encoder for PlayerId=%s"), *Session->GetPlayerId());

		auto VideoEncoder = std::make_unique<FPixelStreamingVideoEncoder>(Session, &EncoderContext);
		this->ActiveEncoders.Add(VideoEncoder.get());
		Session->SetVideoEncoder(VideoEncoder.get());
		return VideoEncoder;
	}
}

void FPixelStreamingVideoEncoderFactory::OnEncodedImage(const webrtc::EncodedImage& encoded_image, const webrtc::CodecSpecificInfo* codec_specific_info, const webrtc::RTPFragmentationHeader* fragmentation)
{
	// Iterate backwards so we can remove invalid encoders along the way
	for (int32 Index = ActiveEncoders.Num()-1; Index >= 0; --Index)
	{
		FPixelStreamingVideoEncoder* Encoder = ActiveEncoders[Index];
		if(!Encoder->IsRegisteredWithWebRTC())
		{
			ActiveEncoders.RemoveAt(Index);
			UE_LOG(PixelStreamer, Log, TEXT("Encoder factory cleaned up stale encoder associated with PlayerId=%s"), *Encoder->GetPlayerId());
		}
		else
		{
			// Fires a callback internally in WebRTC to pass it an encoded image
			Encoder->SendEncodedImage(encoded_image, codec_specific_info, fragmentation);
		}
	}
}

void FPixelStreamingVideoEncoderFactory::ReleaseVideoEncoder(FPixelStreamingVideoEncoder* encoder)
{
	ActiveEncoders.Remove(encoder);
	if (ActiveEncoders.Num() == 0)
	{
		EncoderContext.Encoder->Shutdown();
		EncoderContext.Encoder = nullptr;
	}
	FPlayerId PlayerId = encoder->GetPlayerId();
	UE_LOG(PixelStreamer, Log, TEXT("Encoder factory asked to remove encoder for PlayerId=%s"), *PlayerId);
}
