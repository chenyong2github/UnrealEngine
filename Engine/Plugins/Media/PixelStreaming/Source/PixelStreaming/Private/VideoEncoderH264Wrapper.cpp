// Copyright Epic Games, Inc. All Rights Reserved.

#include "VideoEncoderH264Wrapper.h"
#include "VulkanRHIPrivate.h"
#include "Stats.h"
#include "VideoEncoderFactory.h"
#include "EncoderFactory.h"

UE::PixelStreaming::FVideoEncoderH264Wrapper::FVideoEncoderH264Wrapper(uint64 EncoderId, TUniquePtr<FEncoderFrameFactory> InFrameFactory, TUniquePtr<AVEncoder::FVideoEncoder> InEncoder)
	: Id(EncoderId)
	, FrameFactory(MoveTemp(InFrameFactory))
	, Encoder(MoveTemp(InEncoder)) 
{
	checkf(Encoder, TEXT("Encoder is nullptr."))
}

UE::PixelStreaming::FVideoEncoderH264Wrapper::~FVideoEncoderH264Wrapper()
{
	Encoder->ClearOnEncodedPacket();
	Encoder->Shutdown();
}

void UE::PixelStreaming::FVideoEncoderH264Wrapper::Encode(const webrtc::VideoFrame& WebRTCFrame, bool bKeyframe)
{
	FTexture2DRHIRef SourceTexture;

	UE::PixelStreaming::FFrameBuffer* FrameBuffer = static_cast<UE::PixelStreaming::FFrameBuffer*>(WebRTCFrame.video_frame_buffer().get());
	check(FrameBuffer->GetFrameBufferType() == UE::PixelStreaming::FFrameBufferType::Layer);
	UE::PixelStreaming::FLayerFrameBuffer* LayerFrameBuffer = static_cast<UE::PixelStreaming::FLayerFrameBuffer*>(FrameBuffer);
	SourceTexture = LayerFrameBuffer->GetFrame();

	if (SourceTexture)
	{
		AVEncoder::FVideoEncoder::FLayerConfig EncoderConfig = GetCurrentConfig();
		AVEncoder::FVideoEncoderInputFrame* EncoderInputFrame = FrameFactory->GetFrameAndSetTexture(EncoderConfig.Width, EncoderConfig.Height, SourceTexture);
		if (EncoderInputFrame)
		{
			EncoderInputFrame->SetTimestampUs(WebRTCFrame.timestamp_us());
			EncoderInputFrame->SetTimestampRTP(WebRTCFrame.timestamp());
			EncoderInputFrame->SetFrameID(WebRTCFrame.id());

			AVEncoder::FVideoEncoder::FEncodeOptions Options;
			Options.bForceKeyFrame = bKeyframe || bForceNextKeyframe;
			bForceNextKeyframe = false;

			Encoder->Encode(EncoderInputFrame, Options);
		}
	}
}

AVEncoder::FVideoEncoder::FLayerConfig UE::PixelStreaming::FVideoEncoderH264Wrapper::GetCurrentConfig()
{
	checkf(Encoder, TEXT("Cannot request layer config when encoder is nullptr."));

	// Asume user wants config for layer zero.
	return Encoder->GetLayerConfig(0);
}

void UE::PixelStreaming::FVideoEncoderH264Wrapper::SetConfig(const AVEncoder::FVideoEncoder::FLayerConfig& NewConfig)
{
	checkf(Encoder, TEXT("Cannot set layer config when encoder is nullptr."));

	// Assumer user wants to update layer zero.
	if (NewConfig != GetCurrentConfig())
	{
		if (Encoder)
		{
			Encoder->UpdateLayerConfig(0, NewConfig);
		}
	}
}

/* ------------------ Static functions below --------------------- */

void UE::PixelStreaming::FVideoEncoderH264Wrapper::OnEncodedPacket(uint64 SourceEncoderId, UE::PixelStreaming::FVideoEncoderFactory* Factory, uint32 InLayerIndex, const AVEncoder::FVideoEncoderInputFrame* InFrame, const AVEncoder::FCodecPacket& InPacket)
{
	webrtc::EncodedImage Image;

	webrtc::RTPFragmentationHeader FragHeader;
	//CreateH264FragmentHeader(InPacket.Data.Get(), InPacket.DataSize, FragHeader);

	std::vector<webrtc::H264::NaluIndex> NALUIndices = webrtc::H264::FindNaluIndices(InPacket.Data.Get(), InPacket.DataSize);
	FragHeader.VerifyAndAllocateFragmentationHeader(NALUIndices.size());
	FragHeader.fragmentationVectorSize = static_cast<uint16_t>(NALUIndices.size());
	for (int i = 0; i != NALUIndices.size(); ++i)
	{
		webrtc::H264::NaluIndex const& NALUIndex = NALUIndices[i];
		FragHeader.fragmentationOffset[i] = NALUIndex.payload_start_offset;
		FragHeader.fragmentationLength[i] = NALUIndex.payload_size;
	}

	Image.timing_.packetization_finish_ms = FTimespan::FromSeconds(FPlatformTime::Seconds()).GetTotalMilliseconds();
	Image.timing_.encode_start_ms = InPacket.Timings.StartTs.GetTotalMilliseconds();
	Image.timing_.encode_finish_ms = InPacket.Timings.FinishTs.GetTotalMilliseconds();
	Image.timing_.flags = webrtc::VideoSendTiming::kTriggeredByTimer;

	Image.SetEncodedData(webrtc::EncodedImageBuffer::Create(const_cast<uint8_t*>(InPacket.Data.Get()), InPacket.DataSize));
	Image._encodedWidth = InFrame->GetWidth();
	Image._encodedHeight = InFrame->GetHeight();
	Image._frameType = InPacket.IsKeyFrame ? webrtc::VideoFrameType::kVideoFrameKey : webrtc::VideoFrameType::kVideoFrameDelta;
	Image.content_type_ = webrtc::VideoContentType::UNSPECIFIED;
	Image.qp_ = InPacket.VideoQP;
	Image.SetSpatialIndex(InLayerIndex);
	Image._completeFrame = true;
	Image.rotation_ = webrtc::VideoRotation::kVideoRotation_0;
	Image.SetTimestamp(InFrame->GetTimestampRTP());
	Image.capture_time_ms_ = InFrame->GetTimestampUs() / 1000.0;

	webrtc::CodecSpecificInfo CodecInfo;
	CodecInfo.codecType = webrtc::VideoCodecType::kVideoCodecH264;
	CodecInfo.codecSpecific.H264.packetization_mode = webrtc::H264PacketizationMode::NonInterleaved;
	CodecInfo.codecSpecific.H264.temporal_idx = webrtc::kNoTemporalIdx;
	CodecInfo.codecSpecific.H264.idr_frame = InPacket.IsKeyFrame;
	CodecInfo.codecSpecific.H264.base_layer_sync = false;

	Factory->OnEncodedImage(SourceEncoderId, Image, &CodecInfo, &FragHeader);
}
