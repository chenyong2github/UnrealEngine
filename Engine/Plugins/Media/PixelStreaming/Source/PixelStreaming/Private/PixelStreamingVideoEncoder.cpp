// Copyright Epic Games, Inc. All Rights Reserved.
#include "PixelStreamingVideoEncoder.h"

#include "PixelStreamingEncoderFactory.h"
#include "VideoEncoderFactory.h"
#include "PixelStreamingFrameBuffer.h"
#include "PlayerSession.h"
#include "HUDStats.h"

TAutoConsoleVariable<int32> CVarPixelStreamingEncoderTargetBitrate(
	TEXT("PixelStreaming.Encoder.TargetBitrate"),
	-1,
	TEXT("Target bitrate (bps). Ignore the bitrate WebRTC wants (not recommended). Set to -1 to disable."),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarPixelStreamingEncoderMaxBitrate(
	TEXT("PixelStreaming.Encoder.MaxBitrateVBR"),
	50000000,
	TEXT("Max bitrate (bps). Does not work in CBR rate control mode with NVENC."),
	ECVF_RenderThreadSafe);

TAutoConsoleVariable<int32> CVarPixelStreamingEncoderUseBackBufferSize(
	TEXT("PixelStreaming.Encoder.UseBackBufferSize"),
	3,
	TEXT("Whether to use back buffer size or custom size"),
	ECVF_Cheat);

TAutoConsoleVariable<FString> CVarPixelStreamingEncoderTargetSize(
	TEXT("PixelStreaming.Encoder.TargetSize"),
	TEXT("1920x1080"),
	TEXT("Encoder target size in format widthxheight"),
	ECVF_Cheat);

TAutoConsoleVariable<bool> CVarPixelStreamingDebugDumpFrame(
	TEXT("PixelStreaming.Encoder.DumpDebugFrames"),
	false,
	TEXT("Dumps frames from the encoder to a file on disk for debugging purposes."),
	ECVF_Cheat);

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

TAutoConsoleVariable<bool> CVarPixelStreamingEnableFillerData(
	TEXT("PixelStreaming.Encoder.EnableFillerData"),
	false,
	TEXT("Maintains constant bitrate by filling with junk data."),
	ECVF_Cheat);

TAutoConsoleVariable<int> CVarPixelStreamingLowQpThreshold(
	TEXT("PixelStreaming.WebRTC.LowQpThreshold"),
	24,
	TEXT("Low QP threshold for quality scaling."),
	ECVF_Default);

TAutoConsoleVariable<int> CVarPixelStreamingHighQpThreshold(
	TEXT("PixelStreaming.WebRTC.HighQpThreshold"),
	37,
	TEXT("High QP threshold for quality scaling."),
	ECVF_Default);

using namespace webrtc;

namespace
{
	std::map<FString, AVEncoder::FVideoEncoder::RateControlMode> const RateControlCVarMap{
		{"ConstQP", AVEncoder::FVideoEncoder::RateControlMode::CONSTQP },
		{"VBR", AVEncoder::FVideoEncoder::RateControlMode::VBR },
		{"CBR", AVEncoder::FVideoEncoder::RateControlMode::CBR },
	};

	AVEncoder::FVideoEncoder::RateControlMode GetRateControlCVar()
	{
		auto const cvarStr = CVarPixelStreamingEncoderRateControl.GetValueOnAnyThread();
		auto const it = RateControlCVarMap.find(cvarStr);
		if (it == std::end(RateControlCVarMap))
			return AVEncoder::FVideoEncoder::RateControlMode::CBR;
		return it->second;
}

void CreateH264FragmentHeader(const uint8* CodedData, size_t CodedDataSize, RTPFragmentationHeader& Fragments)
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
			if (offset < (CodedDataSize - 3) && CodedData[offset] == 0 &&
				CodedData[offset + 1] == 0 && CodedData[offset + 2] == 1)
			{
			}
			else if (offset < (CodedDataSize - 4) && CodedData[offset] == 0 &&
				CodedData[offset + 1] == 0 && CodedData[offset + 2] == 0 &&
				CodedData[offset + 3] == 1)
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
}

FPixelStreamingVideoEncoder::FPixelStreamingVideoEncoder(FPlayerSession* OwnerSession, FEncoderContext* context)
{
	verify(OwnerSession);
	verify(context);
	verify(context->Factory)
	bControlsQuality = OwnerSession->IsOriginalQualityController();
	PlayerId = OwnerSession->GetPlayerId();
	Context = context;
}

FPixelStreamingVideoEncoder::~FPixelStreamingVideoEncoder()
{
	Context->Factory->ReleaseVideoEncoder(this);
}

void FPixelStreamingVideoEncoder::SetQualityController(bool bControlsQualityNow)
{
	if (bControlsQuality != bControlsQualityNow)
	{
		UE_LOG(PixelStreamer, Log, TEXT("%s : PlayerId=%s, controls quality %d"), TEXT("FVideoEncoder::SetQualityController"), *this->PlayerId, bControlsQualityNow);
		bControlsQuality = bControlsQualityNow;
	}
}

int FPixelStreamingVideoEncoder::InitEncode(const VideoCodec* codec_settings, const VideoEncoder::Settings& settings)
{
	UE_LOG(PixelStreamer, Log, TEXT("PixelStreaming video encoder initialise for PlayerId=%s"), *this->GetPlayerId());

	if (!bControlsQuality)
		return WEBRTC_VIDEO_CODEC_OK;

	checkf(AVEncoder::FVideoEncoderFactory::Get().IsSetup(), TEXT("FVideoEncoderFactory not setup"));

	VideoInit.Width = codec_settings->width;
	VideoInit.Height = codec_settings->height;
	VideoInit.MaxBitrate = codec_settings->maxBitrate;
	VideoInit.TargetBitrate = codec_settings->startBitrate;
	VideoInit.MaxFramerate = codec_settings->maxFramerate;

	WebRtcProposedTargetBitrate = codec_settings->startBitrate;

	return WEBRTC_VIDEO_CODEC_OK;
}

int32 FPixelStreamingVideoEncoder::RegisterEncodeCompleteCallback(EncodedImageCallback* callback)
{
	UE_LOG(PixelStreamer, Log, TEXT("PixelStreaming video encoder callback registered for PlayerId=%s"), *this->GetPlayerId());
	OnEncodedImageCallback = callback;
	// Encoder is initialised, add it as active encoder to factory.
	return WEBRTC_VIDEO_CODEC_OK;
}

int32 FPixelStreamingVideoEncoder::Release()
{
	UE_LOG(PixelStreamer, Log, TEXT("PixelStreaming video encoder released for PlayerId=%s"), *this->GetPlayerId());
	OnEncodedImageCallback = nullptr;
	return WEBRTC_VIDEO_CODEC_OK;
}

int32 FPixelStreamingVideoEncoder::Encode(const VideoFrame& frame, const std::vector<VideoFrameType>* frame_types)
{
	if (!bControlsQuality)
	{
		//UE_LOG(PixelStreamer, Log, TEXT("Skipping encode for this peer"));
		return WEBRTC_VIDEO_CODEC_OK;
	}

	// Detect video frame not matching encoder encoding resolution
	int FrameWidth = frame.width();
	int FrameHeight = frame.height();
	if(this->Context->Encoder && (this->Context->Encoder->GetWidth(0) != FrameWidth || this->Context->Encoder->GetHeight(0) != FrameHeight))
	{
		this->Context->Encoder->UpdateLayerResolution(0, FrameWidth, FrameHeight);
		this->VideoInit.Width = FrameWidth;
		this->VideoInit.Height = FrameHeight;
	}	

	const FPixelStreamingFrameBuffer* WebRTCVideoFrame = static_cast<FPixelStreamingFrameBuffer*>(frame.video_frame_buffer().get());

	AVEncoder::FVideoEncoderInputFrame* NativeVideoFrame = WebRTCVideoFrame->GetFrame();
	NativeVideoFrame->PTS = frame.timestamp();

	AVEncoder::FVideoEncoder::FEncodeOptions	EncodeOptions;
	
	if (frame_types && (*frame_types)[0] == webrtc::VideoFrameType::kVideoFrameKey || ForceNextKeyframe)
	{
		EncodeOptions.bForceKeyFrame = true;
		ForceNextKeyframe = false;
	}

	if (!Context->Encoder)
	{
		// TODO: When we have multiple HW encoders do some factory checking and find the best encoder.
		auto& Available = AVEncoder::FVideoEncoderFactory::Get().GetAvailable();

		Context->Encoder = AVEncoder::FVideoEncoderFactory::Get().Create(Available[0].ID, WebRTCVideoFrame->GetVideoEncoderInput(), VideoInit);
		Context->Encoder->SetOnEncodedPacket([FactoryPtr = Context->Factory](uint32 InLayerIndex, const AVEncoder::FVideoEncoderInputFrame* InFrame, const AVEncoder::FCodecPacket& InPacket)
		{
			webrtc::EncodedImage Image;
			auto encoded_data = webrtc::EncodedImageBuffer::Create(const_cast<uint8_t*>(InPacket.Data), InPacket.DataSize);
			Image.SetEncodedData(encoded_data);
			Image.SetTimestamp(InPacket.PTS);
			Image._encodedWidth = InFrame->GetWidth();
			Image._encodedHeight = InFrame->GetHeight();
			Image._frameType = InPacket.IsKeyFrame ? VideoFrameType::kVideoFrameKey : VideoFrameType::kVideoFrameDelta;
			Image.content_type_ = VideoContentType::UNSPECIFIED;
			Image.qp_ = InPacket.VideoQP;
			Image.SetSpatialIndex(InLayerIndex);
			Image.timing_.encode_finish_ms = rtc::TimeMicros() / 1000;
			Image.timing_.flags = webrtc::VideoSendTiming::kTriggeredByTimer;
			Image.capture_time_ms_ = InPacket.PTS;
			Image.ntp_time_ms_ = FTimespan::FromSeconds(FPlatformTime::Seconds()).GetTotalMilliseconds();
			Image.timing_.encode_start_ms = InPacket.Timings.StartTs.GetTotalMilliseconds();
			Image._completeFrame = true;
			Image.rotation_ = webrtc::VideoRotation::kVideoRotation_0;

			CodecSpecificInfo CodecInfo;
			CodecInfo.codecType = webrtc::VideoCodecType::kVideoCodecH264;
			CodecInfo.codecSpecific.H264.packetization_mode = webrtc::H264PacketizationMode::NonInterleaved;
			CodecInfo.codecSpecific.H264.temporal_idx = webrtc::kNoTemporalIdx;
			CodecInfo.codecSpecific.H264.idr_frame = InPacket.IsKeyFrame;
			CodecInfo.codecSpecific.H264.base_layer_sync = false;

			RTPFragmentationHeader FragHeader;
			
			CreateH264FragmentHeader(InPacket.Data, InPacket.DataSize, FragHeader);
			FactoryPtr->OnEncodedImage(Image, &CodecInfo, &FragHeader);

			FHUDStats& Stats = FHUDStats::Get();
			double LatencyMs = InPacket.Timings.FinishTs.GetTotalMilliseconds() - InPacket.Timings.StartTs.GetTotalMilliseconds();
			double BitrateMbps = InPacket.DataSize * 8 * InPacket.Framerate / 1000000.0;
			if (Stats.bEnabled)
			{
				Stats.EncoderLatencyMs.Update(LatencyMs);
				Stats.EncoderBitrateMbps.Update(BitrateMbps);
				Stats.EncoderQP.Update(InPacket.VideoQP);
				Stats.EncoderFPS.Update(InPacket.Framerate);
				
			}

			UE_LOG(PixelStreamer, VeryVerbose, TEXT("QP %d/%0.f, latency %.0f/%.0f ms, bitrate %.3f/%.3f Mbps, %d bytes")
				, InPacket.VideoQP, Stats.EncoderQP.Get()
				, LatencyMs, Stats.EncoderLatencyMs.Get()
				, BitrateMbps, Stats.EncoderBitrateMbps.Get()
				, (int)InPacket.DataSize);


		});
	}
	
	// Change encoder settings through CVars

	auto const MaxBitrateCVar = CVarPixelStreamingEncoderMaxBitrate.GetValueOnAnyThread();
	if (MaxBitrateCVar > -1)
		SetMaxBitrate(MaxBitrateCVar);

	auto const TargetBitrateCVar = CVarPixelStreamingEncoderTargetBitrate.GetValueOnAnyThread();
	SetTargetBitrate(TargetBitrateCVar > -1 ? TargetBitrateCVar : WebRtcProposedTargetBitrate);

	auto const MinQPCVar = CVarPixelStreamingEncoderMinQP.GetValueOnAnyThread();
	SetMinQP(MinQPCVar > -1 ? MinQPCVar : 20);

	SetRateControl(GetRateControlCVar());

	auto const FillerDataCVar = CVarPixelStreamingEnableFillerData.GetValueOnAnyThread();
	EnableFillerData(FillerDataCVar);

	// Encode the frame!
	Context->Encoder->Encode(NativeVideoFrame, EncodeOptions);

	return WEBRTC_VIDEO_CODEC_OK;
}

// Pass rate control parameters from WebRTC to our encoder
// This is how WebRTC can control the bitrate/framerate of the encoder.
void FPixelStreamingVideoEncoder::SetRates(const RateControlParameters& parameters)
{
	if (!bControlsQuality)
		return;

	VideoInit.MaxFramerate = parameters.framerate_fps;

	int32 TargetBitrateCVar = CVarPixelStreamingEncoderTargetBitrate.GetValueOnAnyThread();
	// We store what WebRTC wants as the bitrate, even if we are overriding it, so we can restore back to it when user stops using CVar.
	WebRtcProposedTargetBitrate = parameters.bitrate.get_sum_kbps() * 1000; 
	VideoInit.TargetBitrate = TargetBitrateCVar > -1 ? TargetBitrateCVar : WebRtcProposedTargetBitrate;

	if (Context->Encoder)
	{
		Context->Encoder->UpdateLayerBitrate(0, VideoInit.MaxBitrate, VideoInit.TargetBitrate);
		Context->Encoder->UpdateFrameRate(VideoInit.MaxFramerate);
	}
}

// This is mostly used for testing, it may not do anything depending on the rate control mode of the encoder.
// For example, when using NVENC encoder max bitrate is ignored in CBR rate control mode.
void FPixelStreamingVideoEncoder::SetMaxBitrate(int32 MaxBitrate)
{
	if (!bControlsQuality)
		return;

	if(Context->Encoder && VideoInit.MaxBitrate != MaxBitrate)
	{
		VideoInit.MaxBitrate = MaxBitrate;
		Context->Encoder->UpdateLayerBitrate(0, VideoInit.MaxBitrate, VideoInit.TargetBitrate);
	}
}

// This is mostly used for testing, in practice it is advised to let WebRTC set the target bitrate.
void FPixelStreamingVideoEncoder::SetTargetBitrate(int32 TargetBitrate)
{
	if (!bControlsQuality)
		return;

	if(Context->Encoder && VideoInit.TargetBitrate != TargetBitrate)
	{
		VideoInit.TargetBitrate = TargetBitrate;
		Context->Encoder->UpdateLayerBitrate(0, VideoInit.MaxBitrate, VideoInit.TargetBitrate);
	}
}

void FPixelStreamingVideoEncoder::SetMinQP(int32 MinQP)
{
	if (!bControlsQuality)
		return;

	if (Context->Encoder && VideoInit.QPMin != MinQP)
	{
		VideoInit.QPMin = MinQP;
		Context->Encoder->UpdateMinQP(VideoInit.QPMin);
	}
}

void FPixelStreamingVideoEncoder::SetRateControl(AVEncoder::FVideoEncoder::RateControlMode mode)
{
	if (!bControlsQuality)
		return;

	if (Context->Encoder && VideoInit.RateControlMode != mode)
	{
		VideoInit.RateControlMode = mode;
		Context->Encoder->UpdateRateControl(VideoInit.RateControlMode);
	}
}

void FPixelStreamingVideoEncoder::EnableFillerData(bool enable)
{
	if (!bControlsQuality)
		return;

	if (Context->Encoder && VideoInit.FillData != enable)
	{
		VideoInit.FillData = enable;
		Context->Encoder->UpdateFillData(VideoInit.FillData);
	}
}

VideoEncoder::EncoderInfo FPixelStreamingVideoEncoder::GetEncoderInfo() const
{
	VideoEncoder::EncoderInfo info;
	info.supports_native_handle = true;
	info.implementation_name = TCHAR_TO_UTF8(*FString::Printf(TEXT("NVENC_%s"), GDynamicRHI->GetName()));

	auto const lowQp = CVarPixelStreamingLowQpThreshold.GetValueOnAnyThread();
	auto const highQp = CVarPixelStreamingHighQpThreshold.GetValueOnAnyThread();
	info.scaling_settings = VideoEncoder::ScalingSettings(lowQp, highQp);

	return info;
}

void FPixelStreamingVideoEncoder::SendEncodedImage(const webrtc::EncodedImage& encoded_image, const webrtc::CodecSpecificInfo* codec_specific_info, const webrtc::RTPFragmentationHeader* fragmentation)
{

	// Dump H264 frames to file for debugging if CVar is turned on.
	if(CVarPixelStreamingDebugDumpFrame.GetValueOnAnyThread())
	{
		static IFileHandle* FileHandle = nullptr;
		if (!FileHandle)
		{
			IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
			FileHandle = PlatformFile.OpenWrite(TEXT("c:/tmp/nvenc.h264"));
			check(FileHandle);
		}

		FileHandle->Write(encoded_image.data(), encoded_image.size());
		FileHandle->Flush();
	}

	if(OnEncodedImageCallback)
	{
		webrtc::EncodedImageCallback::Result res = OnEncodedImageCallback->OnEncodedImage(encoded_image, codec_specific_info, fragmentation);
	}
}

FPlayerId FPixelStreamingVideoEncoder::GetPlayerId()
{
	return PlayerId;
}

bool FPixelStreamingVideoEncoder::IsRegisteredWithWebRTC()
{
	return OnEncodedImageCallback != nullptr;
}