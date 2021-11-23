// Copyright Epic Games, Inc. All Rights Reserved.
#include "PixelStreamingVideoEncoder.h"
#include "PixelStreamingEncoderFactory.h"
#include "VideoEncoderFactory.h"
#include "VideoCommon.h"
#include "PixelStreamingFrameBuffer.h"
#include "PlayerSession.h"
#include "PixelStreamingStats.h"
#include "UnrealEngine.h"
#include "HAL/PlatformFileManager.h"
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
	Context->Factory->UnregisterVideoEncoder(this->OwnerPlayerId);
}

int FPixelStreamingVideoEncoder::InitEncode(webrtc::VideoCodec const* codec_settings, VideoEncoder::Settings const& settings)
{
	// Note: We don't early exit if this is not the quality controller
	// Because if this does eventually become the quality controller we want the config setup with some real values.
	
	UE_LOG(PixelStreamer, Log, TEXT("PixelStreaming video encoder CONFIG INITIALISED - unassociated with player for now."));

	checkf(AVEncoder::FVideoEncoderFactory::Get().IsSetup(), TEXT("FVideoEncoderFactory not setup"));

	EncoderConfig.Width = codec_settings->width;
	EncoderConfig.Height = codec_settings->height;

	// TODO investigate why codec_settings is not respecting the CVars following code is a bandaid to mitigate a 1 frame quality drop at startup due to this.
	EncoderConfig.MaxBitrate = PixelStreamingSettings::CVarPixelStreamingWebRTCMaxBitrate.GetValueOnAnyThread(); //codec_settings->maxBitrate;
	EncoderConfig.TargetBitrate = PixelStreamingSettings::CVarPixelStreamingWebRTCStartBitrate.GetValueOnAnyThread(); //codec_settings->startBitrate;
	EncoderConfig.MaxFramerate = PixelStreamingSettings::CVarPixelStreamingWebRTCMaxFps.GetValueOnAnyThread(); // codec_settings->maxFramerate;

	WebRtcProposedTargetBitrate = PixelStreamingSettings::CVarPixelStreamingWebRTCStartBitrate.GetValueOnAnyThread();

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

		// Register this encoder with the factory for so it will recieve shared onEncodedCallback
		this->Context->Factory->RegisterVideoEncoder(this->OwnerPlayerId, this);

		// Let any listener know the encoder is now officially ready to receive frames for encoding.
		InitFrameBuffer->OnEncoderInitialized.ExecuteIfBound();

		UE_LOG(PixelStreamer, Log, TEXT("PixelStreaming video encoder ASSOCIATED with PlayerId=%s"), *this->GetPlayerId());

		// When a new encoder is initialised we should force a keyframe as new encoder associated implied a new peer joining.
		this->Context->Factory->ForceKeyFrame();

		return WEBRTC_VIDEO_CODEC_OK;
	}

	// Lock from here on in the case where quality controller is changing mid encode.
	FScopeLock Lock(&this->Context->EncodingCriticalSection);

	FPixelStreamingFrameBuffer* EncoderFrameBuffer = nullptr;

	// We check if the frame is a frame we should "encode", if so, we try to proceed to actually encode it.
	if(VideoFrameBuffer->GetUsageHint() == FPixelStreamingFrameBufferWrapper::EncoderUsageHint::Encode)
	{
		EncoderFrameBuffer = static_cast<FPixelStreamingFrameBuffer*>(VideoFrameBuffer);
	}

	checkf(EncoderFrameBuffer, TEXT("Encoder framebuffer is null, encoding cannot proceed."));
	checkf(this->OwnerPlayerId != INVALID_PLAYER_ID, TEXT("Encoder is not associated with a player/peer, encoding cannot proceed."));

	if(!this->PixelStreamingSessions->IsQualityController(this->OwnerPlayerId))
	{
		UE_LOG(PixelStreamer, Log, TEXT("PixelStreamingVideoEncoder discarding frame - only the quality controlling peer should submit frames for encoding. This can sometimes happen on peer handover, but if you are seeing this continuously this is a bug."));
	}

	// Record stat for when WebRTC delivered frame, if stats are enabled.
	FPixelStreamingStats& Stats = FPixelStreamingStats::Get();
	if(Stats.GetStatsEnabled())
	{
		const int64 DeltaMicros = rtc::TimeMicros() - frame.timestamp_us();
		const double DeltaMillis = (double) DeltaMicros / 1000.0;
		Stats.SetCaptureToEncodeLatency(DeltaMillis);
		Stats.OnWebRTCDeliverFrameForEncode();
	}

	const int FrameWidth = frame.width();
	const int FrameHeight = frame.height();
	if (!Context->Encoder)
	{
		CreateAVEncoder(FrameWidth, FrameHeight);
	}

	// Change rates, if required.
	this->HandlePendingRateChange();

	// Detect video frame not matching encoder encoding resolution
	// Note: This can happen when UE application changes its resolution and the encoder is not programattically updated.
	if (EncoderConfig.Width != FrameWidth || EncoderConfig.Height != FrameHeight)
	{
		EncoderConfig.Width = FrameWidth;
		EncoderConfig.Height = FrameHeight;
		Context->Encoder->UpdateLayerConfig(0, EncoderConfig);
		Context->FrameFactory.SetResolution(FrameWidth, FrameHeight);
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

	AVEncoder::FVideoEncoderInputFrame* EncoderInputFrame = Context->FrameFactory.GetFrameAndSetTexture(FrameWidth, FrameHeight, EncoderFrameBuffer->GetTextureObtainer());
	EncoderInputFrame->SetTimestampRTP(frame.timestamp());
	EncoderInputFrame->SetTimestampUs(frame.timestamp_us());


	// If we are running a latency test then record pre-encode timing
	if (FLatencyTester::IsTestRunning() && FLatencyTester::GetTestStage() == FLatencyTester::ELatencyTestStage::PRE_ENCODE)
	{
		FLatencyTester::RecordPreEncodeTime(EncoderInputFrame->GetFrameID());
	}

	// Encode the frame!
	// TODO technically the encoder can fail here and we should return with WEBRTC_VIDEO_CODEC_ERROR 
	Context->Encoder->Encode(EncoderInputFrame, EncodeOptions);

	// Let factory know we are now post encode, so it is a safe time to do any cleanup as no one is encoding.
	Context->Factory->OnPostEncode();

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

bool FPixelStreamingVideoEncoder::IsInitialized() const
{
	return this->OwnerPlayerId != INVALID_PLAYER_ID;
}

bool FPixelStreamingVideoEncoder::IsRegisteredWithWebRTC()
{
	return OnEncodedImageCallback != nullptr;
}

//Note: this is a free function on purpose as it is not tied to the object life cycle of a given PixelStreamingVideoEncoder
void OnEncodedPacket(FEncoderContext* Context, uint32 InLayerIndex, const AVEncoder::FVideoEncoderInputFrame* InFrame, const AVEncoder::FCodecPacket& InPacket, uint64 StartPostEncodeCycles)
{
	// During shutdown this can function can sometimes be called by a queued encode if timing is unfortunate and the context is null
	if(Context == nullptr)
	{
		return;
	}

	webrtc::EncodedImage Image;

	// Create H264 frag header for the encoded frame
	webrtc::RTPFragmentationHeader FragHeader;
	std::vector<webrtc::H264::NaluIndex> NALUIndices = webrtc::H264::FindNaluIndices(InPacket.Data, InPacket.DataSize);
	FragHeader.VerifyAndAllocateFragmentationHeader(NALUIndices.size());
	FragHeader.fragmentationVectorSize = static_cast<uint16_t>(NALUIndices.size());
	for (int I = 0; I != NALUIndices.size(); ++I)
	{
		webrtc::H264::NaluIndex const& NALUIndex = NALUIndices[I];
		FragHeader.fragmentationOffset[I] = NALUIndex.payload_start_offset;
		FragHeader.fragmentationLength[I] = NALUIndex.payload_size;
	}

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
		uint64 EndPostEncodeCycles = FPlatformTime::Cycles64();
		double PostEncodeMs = FPlatformTime::ToMilliseconds64(EndPostEncodeCycles - StartPostEncodeCycles);
		Stats.SetPostEncodeLatency(PostEncodeMs);
		//UE_LOG(PixelStreamer, Log, TEXT("Post encode ms %f"), PostEncodeMs);

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

void FPixelStreamingVideoEncoder::CreateAVEncoder(int Width, int Height)
{
	// TODO: When we have multiple HW encoders do some factory checking and find the best encoder.
	const TArray<AVEncoder::FVideoEncoderInfo>& Available = AVEncoder::FVideoEncoderFactory::Get().GetAvailable();

	Context->Encoder = AVEncoder::FVideoEncoderFactory::Get().Create(Available[0].ID, Context->FrameFactory.GetOrCreateVideoEncoderInput(Width, Height), EncoderConfig);
	checkf(Context->Encoder, TEXT("Pixel Streaming video encoder creation failed, check encoder config."));

	FEncoderContext* ContextPtr = this->Context;
	Context->Encoder->SetOnEncodedPacket([ContextPtr](uint32 InLayerIndex, const AVEncoder::FVideoEncoderInputFrame* InFrame, const AVEncoder::FCodecPacket& InPacket) 
	{
		uint64 StartPostEncodeCycles = FPlatformTime::Cycles64();
		OnEncodedPacket(ContextPtr, InLayerIndex, InFrame, InPacket, StartPostEncodeCycles);
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