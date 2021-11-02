// Copyright Epic Games, Inc. All Rights Reserved.
#include "PixelStreamingVideoEncoder.h"
#include "PixelStreamingEncoderFactory.h"
#include "VideoEncoderFactory.h"
#include "VideoCommon.h"
#include "PixelStreamingFrameBuffer.h"
#include "PlayerSession.h"
#include "PixelStreamingStats.h"
#include "UnrealEngine.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/Paths.h"
#include "PixelStreamingSettings.h"
#include "LatencyTester.h"
#include "PlayerId.h"
#include "IPixelStreamingSessions.h"
#include "Async/Async.h"
#include "CodecPacket.h"

FPixelStreamingVideoEncoder::FPixelStreamingVideoEncoder(const IPixelStreamingSessions* InPixelStreamingSessions, FEncoderContext* InContext)
	: OwnerPlayerId(INVALID_PLAYER_ID)
{
	verify(InContext);
	verify(InContext->Factory)
	this->Context = InContext;
	this->PixelStreamingSessions = InPixelStreamingSessions;
	this->EncoderConfig = this->CreateEncoderConfigFromCVars(this->EncoderConfig);
}

FPixelStreamingVideoEncoder::~FPixelStreamingVideoEncoder()
{
	Context->Factory->ReleaseVideoEncoder(this);
}

int FPixelStreamingVideoEncoder::InitEncode(webrtc::VideoCodec const* codec_settings, VideoEncoder::Settings const& settings)
{
	// Note: We don't early exit if this is not the quality controller
	// Because if this does eventually become the quality controller we want the config setup with some real values.
	
	UE_LOG(PixelStreamer, Log, TEXT("PixelStreaming video encoder CONFIG INITIALISED - unassociated with player for now."));

	checkf(AVEncoder::FVideoEncoderFactory::Get().IsSetup(), TEXT("FVideoEncoderFactory not setup"));

	EncoderConfig.Width = codec_settings->width;
	EncoderConfig.Height = codec_settings->height;
	EncoderConfig.MaxBitrate = codec_settings->maxBitrate;
	EncoderConfig.TargetBitrate = codec_settings->startBitrate;
	EncoderConfig.MaxFramerate = codec_settings->maxFramerate;

	WebRtcProposedTargetBitrate = codec_settings->startBitrate;

	return WEBRTC_VIDEO_CODEC_OK;
}

int32 FPixelStreamingVideoEncoder::RegisterEncodeCompleteCallback(webrtc::EncodedImageCallback* callback)
{
	UE_LOG(PixelStreamer, Log, TEXT("PixelStreaming video encoder CALLBACK REGISTERED - unassociated with player for now."));
	OnEncodedImageCallback = callback;
	// Encoder is initialised, add it as active encoder to factory.
	return WEBRTC_VIDEO_CODEC_OK;
}

int32 FPixelStreamingVideoEncoder::Release()
{
	UE_LOG(PixelStreamer, Log, TEXT("PixelStreaming video encoder RELEASED for PlayerId=%s"), *this->GetPlayerId());
	OnEncodedImageCallback = nullptr;
	return WEBRTC_VIDEO_CODEC_OK;
}

AVEncoder::FVideoEncoder::FLayerConfig FPixelStreamingVideoEncoder::CreateEncoderConfigFromCVars(AVEncoder::FVideoEncoder::FLayerConfig InEncoderConfig) const
{
	// Change encoder settings through CVars
	const int32 MaxBitrateCVar 											= PixelStreamingSettings::CVarPixelStreamingEncoderMaxBitrate.GetValueOnAnyThread();
	const int32 TargetBitrateCVar 										= PixelStreamingSettings::CVarPixelStreamingEncoderTargetBitrate.GetValueOnAnyThread();
	const int32 MinQPCVar 												= PixelStreamingSettings::CVarPixelStreamingEncoderMinQP.GetValueOnAnyThread();
	const int32 MaxQPCVar 												= PixelStreamingSettings::CVarPixelStreamingEncoderMaxQP.GetValueOnAnyThread();
	const AVEncoder::FVideoEncoder::RateControlMode RateControlCVar 	= PixelStreamingSettings::GetRateControlCVar();
	const AVEncoder::FVideoEncoder::MultipassMode MultiPassCVar 		= PixelStreamingSettings::GetMultipassCVar();
	const bool FillerDataCVar 											= PixelStreamingSettings::CVarPixelStreamingEnableFillerData.GetValueOnAnyThread();
	const AVEncoder::FVideoEncoder::H264Profile H264Profile				= PixelStreamingSettings::GetH264Profile();

	InEncoderConfig.MaxBitrate = MaxBitrateCVar > -1 ? MaxBitrateCVar : InEncoderConfig.MaxBitrate;
	InEncoderConfig.TargetBitrate = TargetBitrateCVar > -1 ? TargetBitrateCVar : WebRtcProposedTargetBitrate;
	InEncoderConfig.QPMin = MinQPCVar;
	InEncoderConfig.QPMax = MaxQPCVar;
	InEncoderConfig.RateControlMode = RateControlCVar;
	InEncoderConfig.MultipassMode = MultiPassCVar;
	InEncoderConfig.FillData = FillerDataCVar;
	InEncoderConfig.H264Profile = H264Profile;

	return InEncoderConfig;
}

int32 FPixelStreamingVideoEncoder::Encode(webrtc::VideoFrame const& frame, std::vector<webrtc::VideoFrameType> const* frame_types)
{
	
	// Get the frame buffer out of the frame
	FPixelStreamingFrameBufferWrapper* VideoFrameBuffer = static_cast<FPixelStreamingFrameBufferWrapper*>(frame.video_frame_buffer().get());

	// Check if this frame is an "initialize the encoder" frame where we associated the encoder with a player id
	if(VideoFrameBuffer->GetUsageHint() == FPixelStreamingFrameBufferWrapper::EncoderUsageHint::Initialize)
	{
		FPixelStreamingInitFrameBuffer* InitFrameBuffer = static_cast<FPixelStreamingInitFrameBuffer*>(VideoFrameBuffer);
		this->OwnerPlayerId = InitFrameBuffer->GetPlayerId();
		// Let any listener know the encoder is now officially ready to receive frames for encoding.
		InitFrameBuffer->OnEncoderInitialized.ExecuteIfBound();

		UE_LOG(PixelStreamer, Log, TEXT("PixelStreaming video encoder ASSOCIATED with PlayerId=%s"), *this->GetPlayerId());

		// When a new encoder is initialised we should force a keyframe as new encoder associated implied a new peer joining.
		this->Context->Factory->ForceKeyFrame();

		return WEBRTC_VIDEO_CODEC_OK;
	}

	FPixelStreamingFrameBuffer* EncoderFrameBuffer = nullptr;

	// We check if the frame is a frame we should "encode", if so, we try to proceed to actually encode it.
	if(VideoFrameBuffer->GetUsageHint() == FPixelStreamingFrameBufferWrapper::EncoderUsageHint::Encode)
	{
		EncoderFrameBuffer = static_cast<FPixelStreamingFrameBuffer*>(VideoFrameBuffer);
	}

	checkf(EncoderFrameBuffer, TEXT("Encoder framebuffer is null, encoding cannot proceed."));
	checkf(this->OwnerPlayerId != INVALID_PLAYER_ID, TEXT("Encoder is not associated with a player/peer, encoding cannot proceed."));

	// Record stat for when WebRTC delivered frame, if stats are enabled.
	FPixelStreamingStats& Stats = FPixelStreamingStats::Get();
	if(Stats.GetStatsEnabled())
	{
		Stats.OnWebRTCDeliverFrameForEncode();
	}

	if (!Context->Encoder)
	{
		CreateAVEncoder(EncoderFrameBuffer->GetVideoEncoderInput());
	}

	// Change rates, if required.
	this->HandlePendingRateChange();

	// Detect video frame not matching encoder encoding resolution
	// Note: This can happen when UE application changes its resolution and the encoder is not programattically updated.
	int const FrameWidth = frame.width();
	int const FrameHeight = frame.height();
	if (EncoderConfig.Width != FrameWidth || EncoderConfig.Height != FrameHeight)
	{
		EncoderConfig.Width = FrameWidth;
		EncoderConfig.Height = FrameHeight;
		Context->Encoder->UpdateLayerConfig(0, EncoderConfig);
	}
	
	// Make a copy of encoder config from the existing config
	AVEncoder::FVideoEncoder::FLayerConfig NewConfig = CreateEncoderConfigFromCVars(EncoderConfig);

	if (NewConfig != EncoderConfig)
	{
		UpdateConfig(NewConfig);
	}

	AVEncoder::FVideoEncoder::FEncodeOptions EncodeOptions;
	if ((frame_types && (*frame_types)[0] == webrtc::VideoFrameType::kVideoFrameKey) || ForceNextKeyframe)
	{
		EncodeOptions.bForceKeyFrame = true;
		ForceNextKeyframe = false;
	}

	AVEncoder::FVideoEncoderInputFrame* const EncoderInputFrame = EncoderFrameBuffer->GetFrame();
	EncoderInputFrame->SetTimestampRTP(frame.timestamp());

	// If we are running a latency test then record pre-encode timing
	if (FLatencyTester::IsTestRunning() && FLatencyTester::GetTestStage() == FLatencyTester::ELatencyTestStage::PRE_ENCODE)
	{
		FLatencyTester::RecordPreEncodeTime(EncoderInputFrame->GetFrameID());
	}

	// Encode the frame!
	Context->Encoder->Encode(EncoderInputFrame, EncodeOptions);

	return WEBRTC_VIDEO_CODEC_OK;
}

void FPixelStreamingVideoEncoder::HandlePendingRateChange()
{
	if(this->PendingRateChange.IsSet())
	{
		const RateControlParameters& RateChangeParams = this->PendingRateChange.GetValue();

		EncoderConfig.MaxFramerate = RateChangeParams.framerate_fps;

		const int32 TargetBitrateCVar = PixelStreamingSettings::CVarPixelStreamingEncoderTargetBitrate.GetValueOnAnyThread();
		// We store what WebRTC wants as the bitrate, even if we are overriding it, so we can restore back to it when user stops using CVar.
		WebRtcProposedTargetBitrate = RateChangeParams.bitrate.get_sum_kbps() * 1000;
		EncoderConfig.TargetBitrate = TargetBitrateCVar > -1 ? TargetBitrateCVar : WebRtcProposedTargetBitrate;

		// Only the quality controlling peer should update the underlying encoder configuration with new bitrate/framerate.
		if (Context->Encoder)
		{
			Context->Encoder->UpdateLayerConfig(0, EncoderConfig);
			UE_LOG(PixelStreamer, Verbose, TEXT("WebRTC changed rates - %d bps | %d fps "), EncoderConfig.TargetBitrate, EncoderConfig.MaxFramerate);
		}

		// Clear the rate change request
		this->PendingRateChange.Reset();
		
	}
}

// Pass rate control parameters from WebRTC to our encoder
// This is how WebRTC can control the bitrate/framerate of the encoder.
void FPixelStreamingVideoEncoder::SetRates(RateControlParameters const& parameters)
{
	this->PendingRateChange.Emplace(parameters);
}

webrtc::VideoEncoder::EncoderInfo FPixelStreamingVideoEncoder::GetEncoderInfo() const
{
	VideoEncoder::EncoderInfo info;
	info.supports_native_handle = true;
	info.is_hardware_accelerated = true;
	info.has_internal_source = false;
	info.supports_simulcast = false;
	info.implementation_name = TCHAR_TO_UTF8(*FString::Printf(TEXT("PIXEL_STREAMING_HW_ENCODER_%s"), GDynamicRHI->GetName()));

	const int LowQP = PixelStreamingSettings::CVarPixelStreamingWebRTCLowQpThreshold.GetValueOnAnyThread();
	const int HighQP = PixelStreamingSettings::CVarPixelStreamingWebRTCHighQpThreshold.GetValueOnAnyThread();
	info.scaling_settings = VideoEncoder::ScalingSettings(LowQP, HighQP);

	// basically means HW encoder must be perfect and drop frames itself etc
	info.has_trusted_rate_controller = false;

	return info;
}

void FPixelStreamingVideoEncoder::UpdateConfig(AVEncoder::FVideoEncoder::FLayerConfig const& InConfig)
{
	this->EncoderConfig = InConfig;

	// Only the quality controlling peer should update the underlying encoder configuration.
	if (this->Context->Encoder)
	{
		this->Context->Encoder->UpdateLayerConfig(0, this->EncoderConfig);
	}
}

void FPixelStreamingVideoEncoder::SendEncodedImage(webrtc::EncodedImage const& encoded_image, webrtc::CodecSpecificInfo const* codec_specific_info, webrtc::RTPFragmentationHeader const* fragmentation)
{
	// Dump H264 frames to file for debugging if CVar is turned on.
	if (PixelStreamingSettings::CVarPixelStreamingDebugDumpFrame.GetValueOnAnyThread())
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

FPlayerId FPixelStreamingVideoEncoder::GetPlayerId() const
{
	return this->OwnerPlayerId;
}

bool FPixelStreamingVideoEncoder::IsRegisteredWithWebRTC()
{
	return OnEncodedImageCallback != nullptr;
}

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

//Note: this is a free function on purpose as it is not tied to the object life cycle of a given PixelStreamingVideoEncoder
void OnEncodedPacket(FEncoderContext* Context, uint32 InLayerIndex, const AVEncoder::FVideoEncoderInputFrame* InFrame, const AVEncoder::FCodecPacketImpl& InPacket)
{
	// During shutdown this can function can sometimes be called by a queued encode if timing is unfortunate and the context is null
	if(Context == nullptr)
	{
		return;
	}

	webrtc::EncodedImage Image;

	webrtc::RTPFragmentationHeader FragHeader;
	CreateH264FragmentHeader(InPacket.Data, InPacket.DataSize, FragHeader);

	Image.timing_.packetization_finish_ms = FTimespan::FromSeconds(FPlatformTime::Seconds()).GetTotalMilliseconds();
	Image.timing_.encode_start_ms = InPacket.Timings.StartTs.GetTotalMilliseconds();
	Image.timing_.encode_finish_ms = InPacket.Timings.FinishTs.GetTotalMilliseconds();
	Image.timing_.flags = webrtc::VideoSendTiming::kTriggeredByTimer;

	Image.SetEncodedData(webrtc::EncodedImageBuffer::Create(const_cast<uint8_t*>(InPacket.Data), InPacket.DataSize));
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

	Context->Factory->OnEncodedImage(Image, &CodecInfo, &FragHeader);

	FPixelStreamingStats& Stats = FPixelStreamingStats::Get();
	const double EncoderLatencyMs = (InPacket.Timings.FinishTs.GetTotalMicroseconds() - InPacket.Timings.StartTs.GetTotalMicroseconds()) / 1000.0;
	const double BitrateMbps = InPacket.DataSize * 8 * InPacket.Framerate / 1000000.0;

	if (Stats.GetStatsEnabled())
	{
		Stats.SetEncoderLatency(EncoderLatencyMs);
		Stats.SetEncoderBitrateMbps(BitrateMbps);
		Stats.SetEncoderQP(InPacket.VideoQP);
		Stats.OnEncodingFinished();
		if(InPacket.IsKeyFrame)
		{
			Stats.OnKeyframeEncoded();
		}
	}

	// If we are running a latency test then record pre-encode timing
	if (FLatencyTester::IsTestRunning() && FLatencyTester::GetTestStage() == FLatencyTester::ELatencyTestStage::POST_ENCODE)
	{
		FLatencyTester::RecordPostEncodeTime(InFrame->GetFrameID());
	}
}

void FPixelStreamingVideoEncoder::CreateAVEncoder(TSharedPtr<AVEncoder::FVideoEncoderInput> EncoderInput)
{
	// TODO: When we have multiple HW encoders do some factory checking and find the best encoder.
	const TArray<AVEncoder::FVideoEncoderInfo>& Available = AVEncoder::FVideoEncoderFactory::Get().GetAvailable();

	Context->Encoder = AVEncoder::FVideoEncoderFactory::Get().Create(Available[0].ID, EncoderInput, EncoderConfig);
	checkf(Context->Encoder, TEXT("Pixel Streaming video encoder creation failed, check encoder config."));

	FEncoderContext* ContextPtr = this->Context;
	Context->Encoder->SetOnEncodedPacket([ContextPtr](uint32 InLayerIndex, const AVEncoder::FVideoEncoderInputFrame* InFrame, const AVEncoder::FCodecPacket& InPacket) 
	{

		// Create a memory copy of the CodecPacket because the encoder will recycle InPacket.Data again after this.
		AVEncoder::FCodecPacketImpl* CopyPacket = new AVEncoder::FCodecPacketImpl();
		CopyPacket->DataSize = InPacket.DataSize;
		CopyPacket->Data = static_cast<const uint8*>(FMemory::Malloc(InPacket.DataSize));
		FMemory::BigBlockMemcpy(const_cast<uint8*>(CopyPacket->Data), InPacket.Data, InPacket.DataSize);
		CopyPacket->IsKeyFrame = InPacket.IsKeyFrame;
		CopyPacket->VideoQP = InPacket.VideoQP;
		CopyPacket->Timings = InPacket.Timings;

		// We do the actual work somewhere on the task graph so we are not locking up the encoder.
		AsyncTask(ENamedThreads::AnyHiPriThreadHiPriTask, [ContextPtr, InLayerIndex, InFrame, CopyPacket] ()
		{
			OnEncodedPacket(ContextPtr, InLayerIndex, InFrame, *CopyPacket);
			delete[] CopyPacket->Data;
			delete CopyPacket;
		}); 
		
	});
}

int32_t FPixelStreamingVideoEncoder::GetSmoothedAverageQP() const
{
	if(this->Context == nullptr)
	{
		return -1;
	}
	return (int32_t)this->Context->SmoothedAvgQP.Get();
}