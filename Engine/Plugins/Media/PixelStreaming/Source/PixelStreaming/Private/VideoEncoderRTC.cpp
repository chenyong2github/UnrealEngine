// Copyright Epic Games, Inc. All Rights Reserved.

#include "VideoEncoderRTC.h"
#include "EncoderFactory.h"
#include "VideoEncoderFactory.h"
#include "FrameBuffer.h"
#include "PlayerSession.h"
#include "Stats.h"
#include "UnrealEngine.h"
#include "Misc/Paths.h"
#include "Settings.h"
#include "PixelStreamingPlayerId.h"
#include "Async/Async.h"
#include "VideoEncoderH264Wrapper.h"
#include "HAL/PlatformFileManager.h"

UE::PixelStreaming::FVideoEncoderRTC::FVideoEncoderRTC(UE::PixelStreaming::FVideoEncoderFactory& InFactory)
	: Factory(InFactory)
{
}

UE::PixelStreaming::FVideoEncoderRTC::~FVideoEncoderRTC()
{
	Factory.ReleaseVideoEncoder(this);
}

int UE::PixelStreaming::FVideoEncoderRTC::InitEncode(webrtc::VideoCodec const* codec_settings, VideoEncoder::Settings const& settings)
{
	checkf(AVEncoder::FVideoEncoderFactory::Get().IsSetup(), TEXT("FVideoEncoderFactory not setup"));

	// Try to create an encoder if it doesnt already.
	TOptional<FVideoEncoderFactory::FHardwareEncoderId> EncoderIdOptional = Factory.GetOrCreateHardwareEncoder(codec_settings->width, codec_settings->height, codec_settings->maxBitrate, codec_settings->startBitrate, codec_settings->maxFramerate);
	
	if(EncoderIdOptional.IsSet())
	{
		HardwareEncoderId = EncoderIdOptional.GetValue();
		UpdateConfig();
		WebRtcProposedTargetBitrate = codec_settings->startBitrate;
		return WEBRTC_VIDEO_CODEC_OK;
	}
	else
	{
		return WEBRTC_VIDEO_CODEC_ERROR;
	}
}

int32 UE::PixelStreaming::FVideoEncoderRTC::RegisterEncodeCompleteCallback(webrtc::EncodedImageCallback* callback)
{
	OnEncodedImageCallback = callback;
	return WEBRTC_VIDEO_CODEC_OK;
}

int32 UE::PixelStreaming::FVideoEncoderRTC::Release()
{
	OnEncodedImageCallback = nullptr;
	return WEBRTC_VIDEO_CODEC_OK;
}

int32 UE::PixelStreaming::FVideoEncoderRTC::Encode(webrtc::VideoFrame const& frame, std::vector<webrtc::VideoFrameType> const* frame_types)
{
	UE::PixelStreaming::FFrameBuffer* FrameBuffer = static_cast<UE::PixelStreaming::FFrameBuffer*>(frame.video_frame_buffer().get());

	// If initialize frame is passed, this is a dummy frame so WebRTC is happy frames are passing through
	// This is used so that an encoder can be active as far as WebRTC is concerned by simply have frames transmitted by some other encoder on its behalf.
	if(FrameBuffer->GetFrameBufferType() == UE::PixelStreaming::FFrameBufferType::Initialize)
	{
		return WEBRTC_VIDEO_CODEC_OK;
	}

	bool bKeyframe = false;
	if ((frame_types && (*frame_types)[0] == webrtc::VideoFrameType::kVideoFrameKey))
	{
		bKeyframe = true;
	}

	UE::PixelStreaming::FVideoEncoderH264Wrapper* Encoder = Factory.GetHardwareEncoder(HardwareEncoderId);
	if (Encoder == nullptr)
	{
		return WEBRTC_VIDEO_CODEC_ERROR;
	}

	UpdateConfig();

	Encoder->Encode(frame, bKeyframe);

	return WEBRTC_VIDEO_CODEC_OK;
}

// Pass rate control parameters from WebRTC to our encoder
// This is how WebRTC can control the bitrate/framerate of the encoder.
void UE::PixelStreaming::FVideoEncoderRTC::SetRates(RateControlParameters const& parameters)
{
	PendingRateChange.Emplace(parameters);
}

webrtc::VideoEncoder::EncoderInfo UE::PixelStreaming::FVideoEncoderRTC::GetEncoderInfo() const
{
	VideoEncoder::EncoderInfo info;
	info.supports_native_handle = true;
	info.is_hardware_accelerated = true;
	info.has_internal_source = false;
	info.supports_simulcast = false;
	info.implementation_name = TCHAR_TO_UTF8(*FString::Printf(TEXT("PIXEL_STREAMING_HW_ENCODER_%s"), GDynamicRHI->GetName()));

	const int LowQP = UE::PixelStreaming::Settings::CVarPixelStreamingWebRTCLowQpThreshold.GetValueOnAnyThread();
	const int HighQP = UE::PixelStreaming::Settings::CVarPixelStreamingWebRTCHighQpThreshold.GetValueOnAnyThread();
	info.scaling_settings = VideoEncoder::ScalingSettings(LowQP, HighQP);

	// if true means HW encoder must be perfect and drop frames itself etc
	info.has_trusted_rate_controller = false;

	return info;
}

void UE::PixelStreaming::FVideoEncoderRTC::UpdateConfig()
{
	if (UE::PixelStreaming::FVideoEncoderH264Wrapper* Encoder = Factory.GetHardwareEncoder(HardwareEncoderId))
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

AVEncoder::FVideoEncoder::FLayerConfig UE::PixelStreaming::FVideoEncoderRTC::CreateEncoderConfigFromCVars(AVEncoder::FVideoEncoder::FLayerConfig InEncoderConfig) const
{
	// Change encoder settings through CVars
	const int32 MaxBitrateCVar = UE::PixelStreaming::Settings::CVarPixelStreamingEncoderMaxBitrate.GetValueOnAnyThread();
	const int32 TargetBitrateCVar = UE::PixelStreaming::Settings::CVarPixelStreamingEncoderTargetBitrate.GetValueOnAnyThread();
	const int32 MinQPCVar = UE::PixelStreaming::Settings::CVarPixelStreamingEncoderMinQP.GetValueOnAnyThread();
	const int32 MaxQPCVar = UE::PixelStreaming::Settings::CVarPixelStreamingEncoderMaxQP.GetValueOnAnyThread();
	const AVEncoder::FVideoEncoder::RateControlMode RateControlCVar = UE::PixelStreaming::Settings::GetRateControlCVar();
	const AVEncoder::FVideoEncoder::MultipassMode MultiPassCVar = UE::PixelStreaming::Settings::GetMultipassCVar();
	const bool bFillerDataCVar = UE::PixelStreaming::Settings::CVarPixelStreamingEnableFillerData.GetValueOnAnyThread();
	const AVEncoder::FVideoEncoder::H264Profile H264Profile = UE::PixelStreaming::Settings::GetH264Profile();

	InEncoderConfig.MaxBitrate = MaxBitrateCVar > -1 ? MaxBitrateCVar : InEncoderConfig.MaxBitrate;
	InEncoderConfig.TargetBitrate = TargetBitrateCVar > -1 ? TargetBitrateCVar : WebRtcProposedTargetBitrate;
	InEncoderConfig.QPMin = MinQPCVar;
	InEncoderConfig.QPMax = MaxQPCVar;
	InEncoderConfig.RateControlMode = RateControlCVar;
	InEncoderConfig.MultipassMode = MultiPassCVar;
	InEncoderConfig.FillData = bFillerDataCVar;
	InEncoderConfig.H264Profile = H264Profile;

	return InEncoderConfig;
}

void UE::PixelStreaming::FVideoEncoderRTC::SendEncodedImage(uint64 SourceEncoderId, webrtc::EncodedImage const& encoded_image, webrtc::CodecSpecificInfo const* codec_specific_info, webrtc::RTPFragmentationHeader const* fragmentation)
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
	if (UE::PixelStreaming::Settings::CVarPixelStreamingDebugDumpFrame.GetValueOnAnyThread())
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
