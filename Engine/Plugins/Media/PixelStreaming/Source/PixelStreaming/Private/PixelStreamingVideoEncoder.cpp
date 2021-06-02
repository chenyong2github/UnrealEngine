// Copyright Epic Games, Inc. All Rights Reserved.
#include "PixelStreamingVideoEncoder.h"

#include "PixelStreamingEncoderFactory.h"
#include "VideoEncoderFactory.h"
#include "PixelStreamingFrameBuffer.h"
#include "PlayerSession.h"
#include "HUDStats.h"
#include "UnrealEngine.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "PixelStreamingSettings.h"
#include "LatencyTester.h"


using namespace webrtc;

namespace
{
	void CreateH264FragmentHeader(uint8 const* CodedData, size_t CodedDataSize, RTPFragmentationHeader& Fragments)
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

int FPixelStreamingVideoEncoder::InitEncode(VideoCodec const* codec_settings, VideoEncoder::Settings const& settings)
{
	UE_LOG(PixelStreamer, Log, TEXT("PixelStreaming video encoder initialise for PlayerId=%s"), *this->GetPlayerId());

	if (!bControlsQuality)
		return WEBRTC_VIDEO_CODEC_OK;

	checkf(AVEncoder::FVideoEncoderFactory::Get().IsSetup(), TEXT("FVideoEncoderFactory not setup"));

	EncoderConfig.Width = codec_settings->width;
	EncoderConfig.Height = codec_settings->height;
	EncoderConfig.MaxBitrate = codec_settings->maxBitrate;
	EncoderConfig.TargetBitrate = codec_settings->startBitrate;
	EncoderConfig.MaxFramerate = codec_settings->maxFramerate;

	WebRtcProposedTargetBitrate = codec_settings->startBitrate;

	// Make viewport match the requested encode size. Is this a good idea?
	// FVector2D ViewportRes = FVector2D(0,0);
    // if (GEngine && GEngine->GameViewport)
    // {
	// 	GEngine->GameViewport->GetViewportSize(ViewportRes);
    // }
	// if(ViewportRes.X != VideoInit.Width || ViewportRes.Y != VideoInit.Height)
	// {

	// 	AsyncTask(ENamedThreads::GameThread, [this]() {
	// 		FSystemResolution::RequestResolutionChange(VideoInit.Width, VideoInit.Height, EWindowMode::Type::Windowed);
	// 	});
		
	// }

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

void FPixelStreamingVideoEncoder::CreateAVEncoder(TSharedPtr<AVEncoder::FVideoEncoderInput> encoderInput)
{
	// TODO: When we have multiple HW encoders do some factory checking and find the best encoder.
	auto& Available = AVEncoder::FVideoEncoderFactory::Get().GetAvailable();

	Context->Encoder = AVEncoder::FVideoEncoderFactory::Get().Create(Available[0].ID, encoderInput, EncoderConfig);
	Context->Encoder->SetOnEncodedPacket([FactoryPtr = Context->Factory](uint32 InLayerIndex, const AVEncoder::FVideoEncoderInputFrame* InFrame, const AVEncoder::FCodecPacket& InPacket)
	{
		webrtc::EncodedImage Image;

		RTPFragmentationHeader FragHeader;
		CreateH264FragmentHeader(InPacket.Data, InPacket.DataSize, FragHeader);

		Image.timing_.packetization_finish_ms = FTimespan::FromSeconds(FPlatformTime::Seconds()).GetTotalMilliseconds();
		Image.timing_.encode_start_ms = InPacket.Timings.StartTs.GetTotalMilliseconds();
		Image.timing_.encode_finish_ms = InPacket.Timings.FinishTs.GetTotalMilliseconds();
		Image.timing_.flags = webrtc::VideoSendTiming::kTriggeredByTimer;

		auto encoded_data = webrtc::EncodedImageBuffer::Create(const_cast<uint8_t*>(InPacket.Data), InPacket.DataSize);
		Image.SetEncodedData(encoded_data);
		Image._encodedWidth = InFrame->GetWidth();
		Image._encodedHeight = InFrame->GetHeight();
		Image._frameType = InPacket.IsKeyFrame ? VideoFrameType::kVideoFrameKey : VideoFrameType::kVideoFrameDelta;
		Image.content_type_ = VideoContentType::UNSPECIFIED;
		Image.qp_ = InPacket.VideoQP;
		Image.SetSpatialIndex(InLayerIndex);
		Image._completeFrame = true;
		Image.rotation_ = webrtc::VideoRotation::kVideoRotation_0;
		Image.SetTimestamp(InPacket.PTS);
		Image.capture_time_ms_ = InPacket.PTS / 1000.0; //presentation timestamp is in microseconds
		Image.ntp_time_ms_ = InFrame->NTP;

		CodecSpecificInfo CodecInfo;
		CodecInfo.codecType = webrtc::VideoCodecType::kVideoCodecH264;
		CodecInfo.codecSpecific.H264.packetization_mode = webrtc::H264PacketizationMode::NonInterleaved;
		CodecInfo.codecSpecific.H264.temporal_idx = webrtc::kNoTemporalIdx;
		CodecInfo.codecSpecific.H264.idr_frame = InPacket.IsKeyFrame;
		CodecInfo.codecSpecific.H264.base_layer_sync = false;

		FactoryPtr->OnEncodedImage(Image, &CodecInfo, &FragHeader);

		FHUDStats& Stats = FHUDStats::Get();
		const double LatencyMs = InPacket.Timings.FinishTs.GetTotalMilliseconds() - InPacket.Timings.StartTs.GetTotalMilliseconds();
		const double BitrateMbps = InPacket.DataSize * 8 * InPacket.Framerate / 1000000.0;
		const double CaptureMs = (InPacket.Timings.StartTs.GetTotalMicroseconds() - InPacket.PTS) / 1000.0;

		if (PixelStreamingSettings::CVarPixelStreamingHudStats.GetValueOnAnyThread())
		{
			Stats.EncoderLatencyMs.Update(LatencyMs);
			Stats.EncoderBitrateMbps.Update(BitrateMbps);
			Stats.EncoderQP.Update(InPacket.VideoQP);
			Stats.EncoderFPS.Update(InPacket.Framerate);
			Stats.CaptureLatencyMs.Update(CaptureMs);
		}

		UE_LOG(PixelStreamer, VeryVerbose, TEXT("QP %d/%0.f, capture latency %.0f ms, latency %.0f/%.0f ms, bitrate %.3f/%.3f Mbps, %d bytes")
			, InPacket.VideoQP, Stats.EncoderQP.Get()
			, Stats.CaptureLatencyMs.Get()
			, LatencyMs, Stats.EncoderLatencyMs.Get()
			, BitrateMbps, Stats.EncoderBitrateMbps.Get()
			, (int)InPacket.DataSize);

		// If we are running a latency test then record pre-encode timing
		if(FLatencyTester::IsTestRunning() && FLatencyTester::GetTestStage() == FLatencyTester::ELatencyTestStage::POST_ENCODE)
		{
			FLatencyTester::RecordPostEncodeTime(InFrame->GetFrameID());
		}

	});
}

int32 FPixelStreamingVideoEncoder::Encode(VideoFrame const& frame, std::vector<VideoFrameType> const* frame_types)
{
	if (!bControlsQuality)
	{
		//UE_LOG(PixelStreamer, Log, TEXT("Skipping encode for this peer"));
		return WEBRTC_VIDEO_CODEC_OK;
	}

	auto const WebRTCVideoFrame = static_cast<FPixelStreamingFrameBuffer*>(frame.video_frame_buffer().get());

	if (!Context->Encoder)
		CreateAVEncoder(WebRTCVideoFrame->GetVideoEncoderInput());

	// Detect video frame not matching encoder encoding resolution
	// Note: This can happen when UE application changes its resolution and the encoder is not programattically.
	int const FrameWidth = frame.width();
	int const FrameHeight = frame.height();
	if (Context->Encoder)
	{
		if (EncoderConfig.Width != FrameWidth || EncoderConfig.Height != FrameHeight)
		{
			EncoderConfig.Width = FrameWidth;
			EncoderConfig.Height = FrameHeight;
			Context->Encoder->UpdateLayerConfig(0, EncoderConfig);
		}
	}
	
	// Change encoder settings through CVars
	const int32 MaxBitrateCVar 											= PixelStreamingSettings::CVarPixelStreamingEncoderMaxBitrate.GetValueOnRenderThread();
	const int32 TargetBitrateCVar 										= PixelStreamingSettings::CVarPixelStreamingEncoderTargetBitrate.GetValueOnRenderThread();
	const int32 MinQPCVar 												= PixelStreamingSettings::CVarPixelStreamingEncoderMinQP.GetValueOnRenderThread();
	const int32 MaxQPCVar 												= PixelStreamingSettings::CVarPixelStreamingEncoderMaxQP.GetValueOnRenderThread();
	const AVEncoder::FVideoEncoder::RateControlMode RateControlCVar 	= PixelStreamingSettings::GetRateControlCVar();
	const AVEncoder::FVideoEncoder::MultipassMode MultiPassCVar 		= PixelStreamingSettings::GetMultipassCVar();
	const bool FillerDataCVar 											= PixelStreamingSettings::CVarPixelStreamingEnableFillerData.GetValueOnRenderThread();

	AVEncoder::FVideoEncoder::FLayerConfig NewConfig = EncoderConfig;
	NewConfig.MaxBitrate = MaxBitrateCVar > -1 ? MaxBitrateCVar : NewConfig.MaxBitrate;
	NewConfig.TargetBitrate = TargetBitrateCVar > -1 ? TargetBitrateCVar : WebRtcProposedTargetBitrate;
	NewConfig.QPMin = MinQPCVar;
	NewConfig.QPMax = MaxQPCVar;
	NewConfig.RateControlMode = RateControlCVar;
	NewConfig.MultipassMode = MultiPassCVar;
	NewConfig.FillData = FillerDataCVar;

	if (NewConfig != EncoderConfig)
	{
		UpdateConfig(NewConfig);
	}

	AVEncoder::FVideoEncoderInputFrame* const NativeVideoFrame = WebRTCVideoFrame->GetFrame();
	NativeVideoFrame->PTS = frame.timestamp();
	NativeVideoFrame->NTP = frame.ntp_time_ms();

	AVEncoder::FVideoEncoder::FEncodeOptions EncodeOptions;
	if (frame_types && (*frame_types)[0] == webrtc::VideoFrameType::kVideoFrameKey || ForceNextKeyframe)
	{
		EncodeOptions.bForceKeyFrame = true;
		ForceNextKeyframe = false;
	}

	// If we are running a latency test then record pre-encode timing
	if(FLatencyTester::IsTestRunning() && FLatencyTester::GetTestStage() == FLatencyTester::ELatencyTestStage::PRE_ENCODE)
	{
		FLatencyTester::RecordPreEncodeTime(NativeVideoFrame->GetFrameID());
	}

	// Encode the frame!
	Context->Encoder->Encode(NativeVideoFrame, EncodeOptions);

	return WEBRTC_VIDEO_CODEC_OK;
}

// Pass rate control parameters from WebRTC to our encoder
// This is how WebRTC can control the bitrate/framerate of the encoder.
void FPixelStreamingVideoEncoder::SetRates(RateControlParameters const& parameters)
{
	EncoderConfig.MaxFramerate = parameters.framerate_fps;

	const int32 TargetBitrateCVar = PixelStreamingSettings::CVarPixelStreamingEncoderTargetBitrate.GetValueOnRenderThread();
	// We store what WebRTC wants as the bitrate, even if we are overriding it, so we can restore back to it when user stops using CVar.
	WebRtcProposedTargetBitrate = parameters.bitrate.get_sum_kbps() * 1000; 
	EncoderConfig.TargetBitrate = TargetBitrateCVar > -1 ? TargetBitrateCVar : WebRtcProposedTargetBitrate;

	if (bControlsQuality && Context->Encoder)
	{
		Context->Encoder->UpdateLayerConfig(0, EncoderConfig);
	}
		
}

VideoEncoder::EncoderInfo FPixelStreamingVideoEncoder::GetEncoderInfo() const
{
	VideoEncoder::EncoderInfo info;
	info.supports_native_handle = true;
	info.is_hardware_accelerated = true;
	info.has_internal_source = false;
	info.supports_simulcast = false;
	info.implementation_name = TCHAR_TO_UTF8(*FString::Printf(TEXT("PIXEL_STREAMING_HW_ENCODER_%s"), GDynamicRHI->GetName()));

	const int LowQP  = PixelStreamingSettings::CVarPixelStreamingWebRTCLowQpThreshold.GetValueOnRenderThread();
	const int HighQP = PixelStreamingSettings::CVarPixelStreamingWebRTCHighQpThreshold.GetValueOnRenderThread();
	info.scaling_settings = VideoEncoder::ScalingSettings(LowQP, HighQP);

	// basically means HW encoder must be perfect and drop frames itself etc
	info.has_trusted_rate_controller = false;

	return info;
}

void FPixelStreamingVideoEncoder::UpdateConfig(AVEncoder::FVideoEncoder::FLayerConfig const& config)
{
	EncoderConfig = config;
	if (bControlsQuality && Context->Encoder)
		Context->Encoder->UpdateLayerConfig(0, EncoderConfig);
}

void FPixelStreamingVideoEncoder::SendEncodedImage(webrtc::EncodedImage const& encoded_image, webrtc::CodecSpecificInfo const* codec_specific_info, webrtc::RTPFragmentationHeader const* fragmentation)
{
	// Dump H264 frames to file for debugging if CVar is turned on.
	if(PixelStreamingSettings::CVarPixelStreamingDebugDumpFrame.GetValueOnRenderThread())
	{
		static IFileHandle* FileHandle = nullptr;
		if (!FileHandle)
		{
			IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
			FString TempFilePath = FPaths::CreateTempFilename(*FPaths::ProjectSavedDir(), TEXT("encoded_frame"), TEXT(".h264"));
			FileHandle = PlatformFile.OpenWrite(*TempFilePath);
			check(FileHandle);
		}

		FileHandle->Write(encoded_image.data(), encoded_image.size());
		FileHandle->Flush();
	}

	if (OnEncodedImageCallback)
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