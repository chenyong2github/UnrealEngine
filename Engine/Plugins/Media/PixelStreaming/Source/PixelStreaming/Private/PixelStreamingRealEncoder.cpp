// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingRealEncoder.h"
#include "Utils.h"
#include "VulkanRHIPrivate.h"
#include "PixelStreamingStats.h"
#include "VideoEncoderFactory.h"
#include "PixelStreamingEncoderFactory.h"

namespace
{
	void CreateH264FragmentHeader(uint8 const* CodedData, size_t CodedDataSize, webrtc::RTPFragmentationHeader& Fragments)
	{
		// count the number of nal units
		for (int pass = 0; pass < 2; ++pass)
		{
			size_t num_nal = 0;
			size_t offset = 0;
			while (offset < CodedDataSize)
			{
				// either a 0,0,1 or 0,0,0,1 sequence indicates a new 'nal'
				size_t nal_maker_length = 3;
				if (offset < (CodedDataSize - 3) && CodedData[offset] == 0 && CodedData[offset + 1] == 0 && CodedData[offset + 2] == 1)
				{
				}
				else if (offset < (CodedDataSize - 4) && CodedData[offset] == 0 && CodedData[offset + 1] == 0 && CodedData[offset + 2] == 0 && CodedData[offset + 3] == 1)
				{
					nal_maker_length = 4;
				}
				else
				{
					++offset;
					continue;
				}
				if (pass == 1)
				{
					Fragments.fragmentationOffset[num_nal] = offset + nal_maker_length;
					Fragments.fragmentationLength[num_nal] = 0;
					if (num_nal > 0)
					{
						Fragments.fragmentationLength[num_nal - 1] = offset - Fragments.fragmentationOffset[num_nal - 1];
					}
				}
				offset += nal_maker_length;
				++num_nal;
			}
			if (pass == 0)
			{
				Fragments.VerifyAndAllocateFragmentationHeader(num_nal);
			}
			else if (pass == 1 && num_nal > 0)
			{
				Fragments.fragmentationLength[num_nal - 1] = offset - Fragments.fragmentationOffset[num_nal - 1];
			}
		}
	}

	//Note: this is a free function on purpose as it is not tied to the object life cycle of a given Encoder
	void OnEncodedPacket(uint64 SourceEncoderId, FPixelStreamingVideoEncoderFactory* Factory, uint32 InLayerIndex, const AVEncoder::FVideoEncoderInputFrame* InFrame, const AVEncoder::FCodecPacket& InPacket)
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
} // namespace

FPixelStreamingRealEncoder::FPixelStreamingRealEncoder(uint64 EncoderId, int Width, int Height, int MaxBitrate, int TargetBitrate, int MaxFramerate, FPixelStreamingVideoEncoderFactory* InFactory)
	: Id(EncoderId)
	, Factory(InFactory)
{
	checkf(Factory, TEXT("Encoder factory is null."));

	EncoderConfig.Width = Width;
	EncoderConfig.Height = Height;
	EncoderConfig.MaxBitrate = MaxBitrate;
	EncoderConfig.TargetBitrate = TargetBitrate;
	EncoderConfig.MaxFramerate = MaxFramerate;

	FrameFactory.SetResolution(EncoderConfig.Width, EncoderConfig.Height);

	// TODO: When we have multiple HW encoders do some factory checking and find the best encoder.
	auto& Available = AVEncoder::FVideoEncoderFactory::Get().GetAvailable();

	Encoder = AVEncoder::FVideoEncoderFactory::Get().Create(Available[0].ID, FrameFactory.GetOrCreateVideoEncoderInput(EncoderConfig.Width, EncoderConfig.Height), EncoderConfig);
	checkf(Encoder, TEXT("Pixel Streaming video encoder creation failed, check encoder config."));

	Encoder->SetOnEncodedPacket([EncoderId, InFactory](uint32 InLayerIndex, const AVEncoder::FVideoEncoderInputFrame* InFrame, const AVEncoder::FCodecPacket& InPacket) {
		OnEncodedPacket(EncoderId, InFactory, InLayerIndex, InFrame, InPacket);
	});
}

FPixelStreamingRealEncoder::~FPixelStreamingRealEncoder()
{
	Encoder->ClearOnEncodedPacket();
	Encoder->Shutdown();
}

void FPixelStreamingRealEncoder::Encode(const webrtc::VideoFrame& WebRTCFrame, bool Keyframe)
{
	FTexture2DRHIRef SourceTexture;

	FPixelStreamingFrameBuffer* FrameBuffer = static_cast<FPixelStreamingFrameBuffer*>(WebRTCFrame.video_frame_buffer().get());
	check(FrameBuffer->GetFrameBufferType() == FPixelStreamingFrameBufferType::Layer);
	FPixelStreamingLayerFrameBuffer* LayerFrameBuffer = static_cast<FPixelStreamingLayerFrameBuffer*>(FrameBuffer);
	SourceTexture = LayerFrameBuffer->GetFrame();

	if (SourceTexture)
	{
		AVEncoder::FVideoEncoderInputFrame* EncoderInputFrame = FrameFactory.GetFrameAndSetTexture(EncoderConfig.Width, EncoderConfig.Height, SourceTexture);
		if (EncoderInputFrame)
		{
			EncoderInputFrame->SetTimestampUs(WebRTCFrame.timestamp_us());
			EncoderInputFrame->SetTimestampRTP(WebRTCFrame.timestamp());
			EncoderInputFrame->SetFrameID(WebRTCFrame.id());

			AVEncoder::FVideoEncoder::FEncodeOptions Options;
			Options.bForceKeyFrame = Keyframe || ForceNextKeyframe;
			ForceNextKeyframe = false;

			Encoder->Encode(EncoderInputFrame, Options);
		}
	}
}

void FPixelStreamingRealEncoder::SetConfig(const AVEncoder::FVideoEncoder::FLayerConfig& NewConfig)
{
	if (NewConfig != EncoderConfig)
	{
		EncoderConfig = NewConfig;

		if (Encoder)
		{
			Encoder->UpdateLayerConfig(0, EncoderConfig);
		}
	}
}
