// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingVideoEncoder.h"
#include "PixelStreamingEncoderFactory.h"
#include "VideoEncoderFactory.h"
#include "PixelStreamingFrameBuffer.h"
#include "PlayerSession.h"
#include "PixelStreamingStats.h"
#include "UnrealEngine.h"
#include "Misc/Paths.h"
#include "PixelStreamingSettings.h"
#include "PlayerId.h"
#include "IPixelStreamingSessions.h"
#include "Async/Async.h"
#include "Utils.h"
#include "PixelStreamingRealEncoder.h"
#include "HAL/PlatformFileManager.h"

FPixelStreamingVideoEncoder::FPixelStreamingVideoEncoder(IPixelStreamingSessions* InPixelStreamingSessions, FPixelStreamingVideoEncoderFactory& InFactory)
	: PixelStreamingSessions(InPixelStreamingSessions)
	, Factory(InFactory)
{
}

FPixelStreamingVideoEncoder::~FPixelStreamingVideoEncoder()
{
	Factory.ReleaseVideoEncoder(this);
}

int FPixelStreamingVideoEncoder::InitEncode(webrtc::VideoCodec const* codec_settings, VideoEncoder::Settings const& settings)
{
	checkf(AVEncoder::FVideoEncoderFactory::Get().IsSetup(), TEXT("FVideoEncoderFactory not setup"));

	// this will cause an encoder to be created if it doesnt already
	HardwareEncoderId = Factory.GetOrCreateHardwareEncoder(codec_settings->width, codec_settings->height, codec_settings->maxBitrate, codec_settings->startBitrate, codec_settings->maxFramerate);
	UpdateConfig();

	WebRtcProposedTargetBitrate = codec_settings->startBitrate;

	return WEBRTC_VIDEO_CODEC_OK;
}

int32 FPixelStreamingVideoEncoder::RegisterEncodeCompleteCallback(webrtc::EncodedImageCallback* callback)
{
	OnEncodedImageCallback = callback;
	return WEBRTC_VIDEO_CODEC_OK;
}

int32 FPixelStreamingVideoEncoder::Release()
{
	OnEncodedImageCallback = nullptr;
	return WEBRTC_VIDEO_CODEC_OK;
}

int32 FPixelStreamingVideoEncoder::Encode(webrtc::VideoFrame const& frame, std::vector<webrtc::VideoFrameType> const* frame_types)
{
	bool Keyframe = false;
	if ((frame_types && (*frame_types)[0] == webrtc::VideoFrameType::kVideoFrameKey))
	{
		Keyframe = true;
	}

	FPixelStreamingRealEncoder* Encoder = Factory.GetHardwareEncoder(HardwareEncoderId);
	if (Encoder == nullptr)
	{
		return WEBRTC_VIDEO_CODEC_ERROR;
	}

	UpdateConfig();

	Encoder->Encode(frame, Keyframe);

	return WEBRTC_VIDEO_CODEC_OK;
}

// Pass rate control parameters from WebRTC to our encoder
// This is how WebRTC can control the bitrate/framerate of the encoder.
void FPixelStreamingVideoEncoder::SetRates(RateControlParameters const& parameters)
{
	PendingRateChange.Emplace(parameters);
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

	// if true means HW encoder must be perfect and drop frames itself etc
	info.has_trusted_rate_controller = false;

	return info;
}

void FPixelStreamingVideoEncoder::UpdateConfig()
{
	if (FPixelStreamingRealEncoder* Encoder = Factory.GetHardwareEncoder(HardwareEncoderId))
	{
		AVEncoder::FVideoEncoder::FLayerConfig NewConfig = Encoder->GetCurrentConfig();

		if (PendingRateChange.IsSet())
		{
			const RateControlParameters& RateChangeParams = PendingRateChange.GetValue();

			NewConfig.MaxFramerate = RateChangeParams.framerate_fps;

			// We store what WebRTC wants as the bitrate, even if we are overriding it, so we can restore back to it when user stops using CVar.
			WebRtcProposedTargetBitrate = RateChangeParams.bitrate.get_sum_kbps() * 1000;

			// Clear the rate change request
			PendingRateChange.Reset();
		}

		// TODO how to handle res changes?

		NewConfig = CreateEncoderConfigFromCVars(NewConfig);

		Encoder->SetConfig(NewConfig);
	}
}

AVEncoder::FVideoEncoder::FLayerConfig FPixelStreamingVideoEncoder::CreateEncoderConfigFromCVars(AVEncoder::FVideoEncoder::FLayerConfig InEncoderConfig) const
{
	// Change encoder settings through CVars
	const int32 MaxBitrateCVar = PixelStreamingSettings::CVarPixelStreamingEncoderMaxBitrate.GetValueOnAnyThread();
	const int32 TargetBitrateCVar = PixelStreamingSettings::CVarPixelStreamingEncoderTargetBitrate.GetValueOnAnyThread();
	const int32 MinQPCVar = PixelStreamingSettings::CVarPixelStreamingEncoderMinQP.GetValueOnAnyThread();
	const int32 MaxQPCVar = PixelStreamingSettings::CVarPixelStreamingEncoderMaxQP.GetValueOnAnyThread();
	const AVEncoder::FVideoEncoder::RateControlMode RateControlCVar = PixelStreamingSettings::GetRateControlCVar();
	const AVEncoder::FVideoEncoder::MultipassMode MultiPassCVar = PixelStreamingSettings::GetMultipassCVar();
	const bool FillerDataCVar = PixelStreamingSettings::CVarPixelStreamingEnableFillerData.GetValueOnAnyThread();
	const AVEncoder::FVideoEncoder::H264Profile H264Profile = PixelStreamingSettings::GetH264Profile();

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

void FPixelStreamingVideoEncoder::SendEncodedImage(uint64 SourceEncoderId, webrtc::EncodedImage const& encoded_image, webrtc::CodecSpecificInfo const* codec_specific_info, webrtc::RTPFragmentationHeader const* fragmentation)
{
	if (SourceEncoderId != HardwareEncoderId)
	{
		return;
	}

	if (FirstKeyframeCountdown > 0)
	{
		// ideally we want to make the first frame of new peers a keyframe but we
		// dont know when webrtc will decide to start sending out frames. this is
		// the next best option. its a count beause when it was the very next frame
		// it still didnt seem to work. delaying it a few frames seems to have worked.
		--FirstKeyframeCountdown;
		if (FirstKeyframeCountdown == 0)
		{
			Factory.ForceKeyFrame();
		}
	}

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
		OnEncodedImageCallback->OnEncodedImage(encoded_image, codec_specific_info, fragmentation);
	}
}
