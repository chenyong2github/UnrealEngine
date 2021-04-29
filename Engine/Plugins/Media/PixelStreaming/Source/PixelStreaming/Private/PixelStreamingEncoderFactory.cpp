// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingEncoderFactory.h"
#include "PlayerSession.h"
#include "PixelStreamingVideoEncoder.h"

using namespace webrtc;

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

std::vector<SdpVideoFormat> FPixelStreamingVideoEncoderFactory::GetSupportedFormats() const
{
	std::vector<webrtc::SdpVideoFormat> video_formats;
	// for (const webrtc::SdpVideoFormat& h264_format : webrtc::SupportedH264Codecs())
	// 	video_formats.push_back(h264_format);

	// TODO (M84FIX): Is this actually the only profile we want to support?
	video_formats.push_back(CreateH264Format(webrtc::H264::kProfileConstrainedBaseline, webrtc::H264::kLevel3_1));
	return video_formats;

	// return CreateH264Format(webrtc::H264::kProfileConstrainedBaseline, webrtc::H264::kLevel3_1) };
}

FPixelStreamingVideoEncoderFactory::CodecInfo FPixelStreamingVideoEncoderFactory::QueryVideoEncoder(const SdpVideoFormat& format) const
{
	CodecInfo codec_info = { false, false };
	codec_info.is_hardware_accelerated = true;
	codec_info.has_internal_source = false;
	return codec_info;
}

std::unique_ptr<VideoEncoder> FPixelStreamingVideoEncoderFactory::CreateVideoEncoder(const SdpVideoFormat& format)
{
	FPlayerSession* Session;
	bool res = PendingPlayerSessions.Dequeue(Session);
	checkf(res, TEXT("no player session associated with encoder instance"));

	UE_LOG(PixelStreamer, Log, TEXT("Encoder factory addded encoder for PlayerId=%d"), Session->GetPlayerId());

	auto VideoEncoder = std::make_unique<FPixelStreamingVideoEncoder>(Session, &EncoderContext);
	this->ActiveEncoders.Add(VideoEncoder.get());
	Session->SetVideoEncoder(VideoEncoder.get());
	return VideoEncoder;
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
			UE_LOG(PixelStreamer, Log, TEXT("Encoder factory cleaned up stale encoder associated with PlayerId=%d"), Encoder->GetPlayerId());
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
	int PlayerId = encoder->GetPlayerId();
	UE_LOG(PixelStreamer, Log, TEXT("Encoder factory asked to remove encoder for PlayerId=%d"), PlayerId);
}

//TODO real encoder class in here
// Maintain TMap<FPlayerId, WeakPtr<ThinEncoder>>