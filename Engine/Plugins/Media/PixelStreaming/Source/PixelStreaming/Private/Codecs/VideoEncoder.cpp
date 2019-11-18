// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "VideoEncoder.h"
#include "RawFrameBuffer.h"
#include "Codecs/PixelStreamingBaseVideoEncoder.h"
#include "PlayerSession.h"
#include "Utils.h"

//
// FVideoEncoderFactory
//

FVideoEncoderFactory::FVideoEncoderFactory(FPixelStreamingBaseVideoEncoder& HWEncoder): HWEncoder(HWEncoder)
{}

void FVideoEncoderFactory::AddSession(FPlayerSession& PlayerSession)
{
	PendingPlayerSessions.Enqueue(&PlayerSession);
}

std::vector<webrtc::SdpVideoFormat> FVideoEncoderFactory::GetSupportedFormats() const
{
	// return { CreateH264Format(webrtc::H264::kProfileBaseline, webrtc::H264::kLevel3_1),
	//	CreateH264Format(webrtc::H264::kProfileConstrainedBaseline, webrtc::H264::kLevel3_1) };
	// return { CreateH264Format(webrtc::H264::kProfileMain, webrtc::H264::kLevel3_1) };
	return {CreateH264Format(webrtc::H264::kProfileConstrainedBaseline, webrtc::H264::kLevel5_2)};
	// return { CreateH264Format(webrtc::H264::kProfileHigh, webrtc::H264::kLevel5_1) };
}

webrtc::VideoEncoderFactory::CodecInfo
FVideoEncoderFactory::QueryVideoEncoder(const webrtc::SdpVideoFormat& Format) const
{
	CodecInfo Info;
	Info.is_hardware_accelerated = true;
	Info.has_internal_source = false;
	return Info;
}

std::unique_ptr<webrtc::VideoEncoder> FVideoEncoderFactory::CreateVideoEncoder(const webrtc::SdpVideoFormat& Format)
{
	FPlayerSession* Session;
	bool res = PendingPlayerSessions.Dequeue(Session);
	checkf(res, TEXT("no player session associated with encoder instance"));

	auto VideoEncoder = std::make_unique<FVideoEncoder>(HWEncoder, *Session);
	Session->SetVideoEncoder(VideoEncoder.get());
	return VideoEncoder;
}

//
// FVideoEncoder
//

FVideoEncoder::FVideoEncoder(FPixelStreamingBaseVideoEncoder& HWEncoder, FPlayerSession& InPlayerSession):
	HWEncoder(HWEncoder),
	PlayerSession(&InPlayerSession)
{
	check(PlayerSession);

	bControlsQuality = PlayerSession->IsOriginalQualityController();

	CodecSpecific.codecType = webrtc::kVideoCodecH264;
	// #TODO: Probably smarter setting of `packetization_mode` is required, look at `H264EncoderImpl` ctor
	// CodecSpecific.codecSpecific.H264.packetization_mode = webrtc::H264PacketizationMode::SingleNalUnit;
	CodecSpecific.codecSpecific.H264.packetization_mode = webrtc::H264PacketizationMode::NonInterleaved;

	UE_LOG(PixelStreamer, Log, TEXT("WebRTC VideoEncoder created%s"), bControlsQuality? TEXT(", quality controller"): TEXT(""));
}

FVideoEncoder::~FVideoEncoder()
{
	UE_LOG(PixelStreamer, Log, TEXT("WebRTC VideoEncoder destroyed"));
}

void FVideoEncoder::SetQualityController(bool bControlsQualityNow)
{
	if (bControlsQuality != bControlsQualityNow)
	{
		UE_LOG(PixelStreamer, Log, TEXT("%s : PlayerId=%d, controls quality %d"), TEXT(__FUNCTION__), PlayerSession->GetPlayerId(), bControlsQualityNow);
		bControlsQuality = bControlsQualityNow;
	}
}

int32 FVideoEncoder::InitEncode(const webrtc::VideoCodec* CodecSettings, int32 NumberOfCores, size_t MaxPayloadSize)
{
	return 0;
}

int32 FVideoEncoder::RegisterEncodeCompleteCallback(webrtc::EncodedImageCallback* InCallback)
{
	Callback = InCallback;
	HWEncoder.SubscribeToFrameEncodedEvent(*this);
	return 0;
}

int32 FVideoEncoder::Release()
{
	HWEncoder.UnsubscribeFromFrameEncodedEvent(*this);
	Callback = nullptr;
	return 0;
}

int32 FVideoEncoder::Encode(const webrtc::VideoFrame& Frame, const webrtc::CodecSpecificInfo* CodecSpecificInfo, const std::vector<webrtc::FrameType>* FrameTypes)
{
	if (!bControlsQuality)
		return WEBRTC_VIDEO_CODEC_OK;

	UE_LOG(PixelStreamer, VeryVerbose, TEXT("(%d) encode ts %u, ts_us %llu, ntp_ts_ms %llu, render_ts_ms %llu"), RtcTimeMs(), Frame.timestamp(), Frame.timestamp_us(), Frame.ntp_time_ms(), Frame.render_time_ms());

	FRawFrameBuffer* RawFrame = static_cast<FRawFrameBuffer*>(Frame.video_frame_buffer().get());
	// the frame managed to pass encoder queue so disable frame drop notification
	RawFrame->DisableFrameDropNotification();

	FBufferId BufferId = RawFrame->GetBuffer();

	webrtc::EncodedImage EncodedImage;
	EncodedImage._completeFrame = true;
	EncodedImage.SetTimestamp(Frame.timestamp());
	EncodedImage.ntp_time_ms_ = Frame.ntp_time_ms();
	EncodedImage.capture_time_ms_ = Frame.render_time_ms();
	EncodedImage.rotation_ = Frame.rotation();

	if (FrameTypes && (*FrameTypes)[0] == webrtc::kVideoFrameKey)
	{
		UE_LOG(PixelStreamer, Verbose, TEXT("key-frame requested, size=%zu"), FrameTypes->size());
		EncodedImage._frameType = webrtc::kVideoFrameKey;
	}

	// TODO(andriy): `LastBitrate.get_sum_kbps()` most probably includes audio bitrate too,
	// check if this causes any packet drops
	HWEncoder.EncodeFrame(BufferId, EncodedImage, LastBitrate.get_sum_bps());

	return WEBRTC_VIDEO_CODEC_OK;
}

void FVideoEncoder::OnEncodedFrame(const webrtc::EncodedImage& EncodedImage)
{
	// fill RTP fragmentation info
	std::vector<webrtc::H264::NaluIndex> NALUIndices =
		webrtc::H264::FindNaluIndices(EncodedImage._buffer, EncodedImage._length);
	FragHeader.VerifyAndAllocateFragmentationHeader(NALUIndices.size());
	FragHeader.fragmentationVectorSize = static_cast<uint16_t>(NALUIndices.size());
	for (int I = 0; I != NALUIndices.size(); ++I)
	{
		webrtc::H264::NaluIndex const& NALUIndex = NALUIndices[I];
		FragHeader.fragmentationOffset[I] = NALUIndex.payload_start_offset;
		FragHeader.fragmentationLength[I] = NALUIndex.payload_size;
	}

	UE_LOG(PixelStreamer, VeryVerbose, TEXT("(%d) encoded ts %u, ntp_ts_ms %lld, capture_ts_ms %lld"), RtcTimeMs(), EncodedImage.Timestamp(), EncodedImage.ntp_time_ms_, EncodedImage.capture_time_ms_);

	// Deliver encoded image.
	Callback->OnEncodedImage(EncodedImage, &CodecSpecific, &FragHeader);

	//// lame video recording to a file
	//static IFileHandle* FileHandle = nullptr;
	//if (!FileHandle)
	//{
	//	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	//	FileHandle = PlatformFile.OpenWrite(TEXT("c:/tmp/nvenc.h264"));
	//	check(FileHandle);
	//}

	//FileHandle->Write(EncodedImage._buffer, EncodedImage._length);
	//FileHandle->Flush();
}

int32 FVideoEncoder::SetChannelParameters(uint32 PacketLoss, int64 Rtt)
{
	return 0;
}

int32 FVideoEncoder::SetRates(uint32 Bitrate, uint32 Framerate)
{
	checkNoEntry(); // unexpected call, if even happens, check if passed Bitrate/Framerate should be taken into account
	return 0;
}

int32 FVideoEncoder::SetRateAllocation(const webrtc::BitrateAllocation& Allocation, uint32 Framerate)
{
	LastBitrate = Allocation;
	LastFramerate = Framerate;

	if (bControlsQuality)
	{
		UE_LOG(PixelStreamer, Log, TEXT("%s : PlayerId=%d, Bitrate=%u kbps, framerate=%u"), TEXT(__FUNCTION__), PlayerSession->GetPlayerId(), Allocation.get_sum_kbps(), Framerate);
	}

	return 0;
}

webrtc::VideoEncoder::ScalingSettings FVideoEncoder::GetScalingSettings() const
{
	// verifySlow(false);
	// return ScalingSettings{ ScalingSettings::kOff };
	return ScalingSettings{0, 1024 * 1024};
}

bool FVideoEncoder::SupportsNativeHandle() const
{
	return true;
}

std::vector<webrtc::SdpVideoFormat> FDummyVideoEncoderFactory::GetSupportedFormats() const
{
	return { CreateH264Format(webrtc::H264::kProfileConstrainedBaseline, webrtc::H264::kLevel5_2) };
}
