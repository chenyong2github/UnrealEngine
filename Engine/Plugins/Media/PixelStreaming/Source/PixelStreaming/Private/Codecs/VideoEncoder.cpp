// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "VideoEncoder.h"
#include "RawFrameBuffer.h"
#include "PlayerSession.h"
#include "Utils.h"
#include "HUDStats.h"
#include "Async/Async.h"

TAutoConsoleVariable<int32> CVarPixelStreamingEncoderMaxBitrate(
	TEXT("PixelStreaming.Encoder.MaxBitrate"),
	50000000,
	TEXT("Max bitrate no matter what WebRTC says, in Bps"),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarPixelStreamingEncoderUseBackBufferSize(
	TEXT("PixelStreaming.Encoder.UseBackBufferSize"),
	1,
	TEXT("Whether to use back buffer size or custom size"),
	ECVF_Cheat);

TAutoConsoleVariable<FString> CVarPixelStreamingEncoderTargetSize(
	TEXT("PixelStreaming.Encoder.TargetSize"),
	TEXT("1920x1080"),
	TEXT("Encoder target size in format widthxheight"),
	ECVF_Cheat);

TAutoConsoleVariable<int32> CVarPixelStreamingEncoderPrioritizeQuality(
	TEXT("PixelStreaming.Encoder.PrioritizeQuality"),
	0,
	TEXT("Reduces framerate automatically on bitrate reduction to trade FPS/latency for video quality"),
	ECVF_Cheat);

TAutoConsoleVariable<int32> CVarPixelStreamingEncoderLowBitrate(
	TEXT("PixelStreaming.Encoder.LowBitrate"),
	1000000,
	TEXT("Lower bound of bitrate for quality adaptation, in Bps"),
	ECVF_Default);

TAutoConsoleVariable<int32> CVarPixelStreamingEncoderHighBitrate(
	TEXT("PixelStreaming.Encoder.HighBitrate"),
	5000000,
	TEXT("Upper bound of bitrate for quality adaptation, in Bps"),
	ECVF_Default);

TAutoConsoleVariable<float> CVarPixelStreamingEncoderMinFPS(
	TEXT("PixelStreaming.Encoder.MinFPS"),
	10.0,
	TEXT("Minimal FPS for quality adaptation"),
	ECVF_Default);

TAutoConsoleVariable<int32> CVarPixelStreamingEncoderMinQP(
	TEXT("PixelStreaming.Encoder.MinQP"),
	20,
	TEXT("0-51, lower values result in better quality but higher bitrate"),
	ECVF_Default);

TAutoConsoleVariable<FString> CVarPixelStreamingEncoderRateControl(
	TEXT("PixelStreaming.Encoder.RateControl"),
	TEXT("CBR"),
	TEXT("PixelStreaming video encoder RateControl mode. Supported modes are `ConstQP`, `VBR`, `CBR`"),
	ECVF_Default);


//
// FVideoEncoderFactory
//

FVideoEncoderFactory::FVideoEncoderFactory(FHWEncoderDetails& InHWEncoderDetails): HWEncoderDetails(InHWEncoderDetails)
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

	auto VideoEncoder = std::make_unique<FVideoEncoder>(HWEncoderDetails, *Session);
	Session->SetVideoEncoder(VideoEncoder.get());
	return VideoEncoder;
}

//
// FVideoEncoder
//

FVideoEncoder::FVideoEncoder(FHWEncoderDetails& InHWEncoderDetails, FPlayerSession& InPlayerSession):
	HWEncoderDetails(InHWEncoderDetails),
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
	HWEncoderDetails.Encoder->RegisterListener(*this);
	return 0;
}

int32 FVideoEncoder::Release()
{
	HWEncoderDetails.Encoder->UnregisterListener(*this);
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

	AVEncoder::FBufferId BufferId = RawFrame->GetBuffer();

	auto EncoderCookie = MakeUnique<FEncoderCookie>();
	EncoderCookie->EncodedImage._completeFrame = true;
	EncoderCookie->EncodedImage.SetTimestamp(Frame.timestamp());
	EncoderCookie->EncodedImage.ntp_time_ms_ = Frame.ntp_time_ms();
	EncoderCookie->EncodedImage.capture_time_ms_ = Frame.render_time_ms();
	EncoderCookie->EncodedImage.rotation_ = Frame.rotation();
	EncoderCookie->EncodedImage.timing_.encode_start_ms = rtc::TimeMicros() / 1000;

	bool bForceKeyFrame = false;
	if (FrameTypes && (*FrameTypes)[0] == webrtc::kVideoFrameKey)
	{
		UE_LOG(PixelStreamer, Verbose, TEXT("key-frame requested, size=%zu"), FrameTypes->size());
		bForceKeyFrame = true;
	}

	// Adjust framerate if required, so we can keep quality without increasing bitrate
	{
		float Fps;
		if (!CVarPixelStreamingEncoderPrioritizeQuality.GetValueOnAnyThread())
		{
			Fps = HWEncoderDetails.InitialMaxFPS;
		}
		else
		{

			// Quality of video suffers if B/W is limited and drops below some threshold. We can sacrifice
			// responsiveness (latency) to improve video quality. We reduce framerate and so encoder
			// can spread limited bitrate over fewer frames.
			int32 Bps = HWEncoderDetails.LastBitrate;

			// bitrate lower than lower bound results always in min FPS
			// bitrate between lower and upper bounds results in FPS proportionally between min and max FPS
			// bitrate higher than upper bound results always in max FPS
			const int32 UpperBoundBps = CVarPixelStreamingEncoderHighBitrate.GetValueOnAnyThread();
			const int32 LowerBoundBps = FMath::Min(CVarPixelStreamingEncoderLowBitrate.GetValueOnAnyThread(), UpperBoundBps);
			const float MaxFps = HWEncoderDetails.InitialMaxFPS;
			const float MinFps = FMath::Min(CVarPixelStreamingEncoderMinFPS.GetValueOnAnyThread(), MaxFps);

			if (Bps < LowerBoundBps)
				Fps = MinFps;
			else if (Bps < UpperBoundBps)
			{
				Fps = MinFps + (MaxFps - MinFps) / (UpperBoundBps - LowerBoundBps) * (Bps - LowerBoundBps);
			}
			else
			{
				Fps = MaxFps;
			}
		}

		if (HWEncoderDetails.LastFramerate != int(Fps))
		{
			HWEncoderDetails.LastFramerate = int(Fps);
			// SetMaxFPS must be called from the game thread because it changes a console var
			AsyncTask(ENamedThreads::GameThread, [Fps = int(Fps)]() { GEngine->SetMaxFPS(Fps); });
			HWEncoderDetails.Encoder->SetFramerate(HWEncoderDetails.LastFramerate);
		}
	}

	// Adjust QP
	{
		int MinQP = CVarPixelStreamingEncoderMinQP.GetValueOnAnyThread();
		MinQP = FMath::Clamp(MinQP, 0, 51);
		if (HWEncoderDetails.LastQP != MinQP)
		{
			HWEncoderDetails.LastQP = MinQP;
			HWEncoderDetails.Encoder->SetParameter(TEXT("qp"), FString::FromInt(HWEncoderDetails.LastQP));
		}
	}

	// Adjust RcMode
	{
		FString RcMode = CVarPixelStreamingEncoderRateControl.GetValueOnAnyThread();
		if (HWEncoderDetails.LastRcMode != RcMode)
		{
			HWEncoderDetails.LastRcMode = RcMode;
			HWEncoderDetails.Encoder->SetParameter(TEXT("ratecontrolmode"), HWEncoderDetails.LastRcMode);
		}
	}

	// TODO(andriy): `LastBitrate.get_sum_kbps()` most probably includes audio bitrate too,
	// check if this causes any packet drops
	HWEncoderDetails.LastBitrate = FMath::Min((uint32)LastBitrate.get_sum_bps(), (uint32)CVarPixelStreamingEncoderMaxBitrate.GetValueOnAnyThread());
	HWEncoderDetails.Encoder->Encode(BufferId, bForceKeyFrame, HWEncoderDetails.LastBitrate, MoveTemp(EncoderCookie));

	return WEBRTC_VIDEO_CODEC_OK;
}

void FVideoEncoder::OnEncodedVideoFrame(const AVEncoder::FAVPacket& Packet, AVEncoder::FEncoderVideoFrameCookie* CookieIn)
{
	FEncoderCookie* Cookie = static_cast<FEncoderCookie*>(CookieIn);

	// Check if the encoder dropped it for some reason
	if (!Packet.IsValid())
	{
		Callback->OnDroppedFrame(webrtc::EncodedImageCallback::DropReason::kDroppedByEncoder);
		UE_LOG(PixelStreamer, Log, TEXT("Dropping frame due to encoder failure"));
		return;
	}

	// The hardware encoder is shared between webrtc's FVideoEncoder instances, so we only create the EncodedImage buffer once
	if (Cookie->EncodedFrameBuffer.Num()==0)
	{
		Cookie->EncodedImage._encodedWidth = Packet.Video.Width;
		Cookie->EncodedImage._encodedHeight = Packet.Video.Height;
		Cookie->EncodedImage.timing_.encode_finish_ms = rtc::TimeMicros() / 1000;
		Cookie->EncodedImage.timing_.flags = webrtc::VideoSendTiming::kTriggeredByTimer;
		Cookie->EncodedImage._frameType = Packet.IsVideoKeyFrame() ? webrtc::kVideoFrameKey : webrtc::kVideoFrameDelta;
		Cookie->EncodedImage.qp_ = Packet.Video.FrameAvgQP;
		Cookie->EncodedFrameBuffer = Packet.Data;
		Cookie->EncodedImage._buffer = Cookie->EncodedFrameBuffer.GetData();
		Cookie->EncodedImage._length = Cookie->EncodedFrameBuffer.Num();

		FHUDStats& Stats = FHUDStats::Get();
		double LatencyMs = Packet.Timings.EncodeFinishTs.GetTotalMilliseconds() - Packet.Timings.EncodeStartTs.GetTotalMilliseconds();
		double BitrateMbps = Packet.Data.Num() * 8 * Packet.Video.Framerate / 1000000.0;
		if (Stats.bEnabled)
		{
			Stats.EncoderLatencyMs.Update(LatencyMs);
			Stats.EncoderBitrateMbps.Update(BitrateMbps);
			Stats.EncoderQP.Update(Packet.Video.FrameAvgQP);
		}

		UE_LOG(PixelStreamer, VeryVerbose, TEXT("QP %d/%0.f, latency %.0f/%.0f ms, bitrate %.3f/%.3f Mbps, %d bytes")
			, Packet.Video.FrameAvgQP, Stats.EncoderQP.Get()
			, LatencyMs, Stats.EncoderLatencyMs.Get()
			, BitrateMbps, Stats.EncoderBitrateMbps.Get()
			, (int)Packet.Data.Num());

	}

	// fill RTP fragmentation info
	std::vector<webrtc::H264::NaluIndex> NALUIndices =
		webrtc::H264::FindNaluIndices(Cookie->EncodedImage._buffer, Cookie->EncodedImage._length);
	FragHeader.VerifyAndAllocateFragmentationHeader(NALUIndices.size());
	FragHeader.fragmentationVectorSize = static_cast<uint16_t>(NALUIndices.size());
	for (int I = 0; I != NALUIndices.size(); ++I)
	{
		webrtc::H264::NaluIndex const& NALUIndex = NALUIndices[I];
		FragHeader.fragmentationOffset[I] = NALUIndex.payload_start_offset;
		FragHeader.fragmentationLength[I] = NALUIndex.payload_size;
	}

	UE_LOG(PixelStreamer, VeryVerbose, TEXT("(%d) encoded ts %u, ntp_ts_ms %lld, capture_ts_ms %lld"), RtcTimeMs(), Cookie->EncodedImage.Timestamp(), Cookie->EncodedImage.ntp_time_ms_, Cookie->EncodedImage.capture_time_ms_);

	// Deliver encoded image.
	Callback->OnEncodedImage(Cookie->EncodedImage, &CodecSpecific, &FragHeader);

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
