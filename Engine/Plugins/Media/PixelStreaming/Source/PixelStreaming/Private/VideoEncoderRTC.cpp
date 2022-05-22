// Copyright Epic Games, Inc. All Rights Reserved.

#include "VideoEncoderRTC.h"
#include "VideoEncoderFactorySimple.h"
#include "VideoEncoderFactory.h"
#include "FrameBufferH264.h"
#include "FrameAdapterH264.h"
#include "Settings.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "Stats.h"

namespace UE::PixelStreaming
{
	FVideoEncoderRTC::FVideoEncoderRTC(FVideoEncoderFactorySimple& InFactory)
		: Factory(InFactory)
	{
	}

	FVideoEncoderRTC::~FVideoEncoderRTC()
	{
		Factory.ReleaseVideoEncoder(this);
	}

	int FVideoEncoderRTC::InitEncode(webrtc::VideoCodec const* InCodecSettings, VideoEncoder::Settings const& settings)
	{
		checkf(AVEncoder::FVideoEncoderFactory::Get().IsSetup(), TEXT("FVideoEncoderFactory not setup"));
		// Try and get the encoder. If it doesn't exist, create it.
		FVideoEncoderH264Wrapper* Encoder = Factory.GetOrCreateHardwareEncoder(InCodecSettings->width, InCodecSettings->height, InCodecSettings->maxBitrate, InCodecSettings->startBitrate, InCodecSettings->maxFramerate);

		if (Encoder != nullptr)
		{
			HardwareEncoder = Encoder;
			UpdateConfig();
			WebRtcProposedTargetBitrate = InCodecSettings->startBitrate;
			return WEBRTC_VIDEO_CODEC_OK;
		}
		else
		{
			return WEBRTC_VIDEO_CODEC_ERROR;
		}
	}

	int32 FVideoEncoderRTC::RegisterEncodeCompleteCallback(webrtc::EncodedImageCallback* callback)
	{
		OnEncodedImageCallback = callback;
		return WEBRTC_VIDEO_CODEC_OK;
	}

	int32 FVideoEncoderRTC::Release()
	{
		OnEncodedImageCallback = nullptr;
		return WEBRTC_VIDEO_CODEC_OK;
	}

	int32 FVideoEncoderRTC::Encode(webrtc::VideoFrame const& frame, std::vector<webrtc::VideoFrameType> const* frame_types)
	{
		FFrameBufferH264* FrameBuffer = static_cast<FFrameBufferH264*>(frame.video_frame_buffer().get());

		// If initialize frame is passed, this is a dummy frame so WebRTC is happy frames are passing through
		// This is used so that an encoder can be active as far as WebRTC is concerned by simply have frames transmitted by some other encoder on its behalf.
		if (FrameBuffer->GetFrameBufferType() == EPixelStreamingFrameBufferType::Initialize)
		{
			return WEBRTC_VIDEO_CODEC_OK;
		}

		FPixelStreamingFrameMetadata& FrameMetadata = FrameBuffer->GetAdaptedLayer()->Metadata;
		FrameMetadata.UseCount++;
		FrameMetadata.LastEncodeStartTime = FPlatformTime::Cycles64();
		if (FrameMetadata.UseCount == 1)
		{
			FrameMetadata.FirstEncodeStartTime = FrameMetadata.LastEncodeStartTime;
		}

		bool bKeyframe = false;
		if ((frame_types && (*frame_types)[0] == webrtc::VideoFrameType::kVideoFrameKey))
		{
			bKeyframe = true;
		}

		FVideoEncoderH264Wrapper* Encoder = Factory.GetHardwareEncoder();
		if (Encoder == nullptr)
		{
			return WEBRTC_VIDEO_CODEC_ERROR;
		}

		UpdateConfig();

		Encoder->Encode(frame, bKeyframe);

		FrameMetadata.LastEncodeEndTime = FPlatformTime::Cycles64();

		FStats::Get()->AddFrameTimingStats(FrameMetadata);

		return WEBRTC_VIDEO_CODEC_OK;
	}

	// Pass rate control parameters from WebRTC to our encoder
	// This is how WebRTC can control the bitrate/framerate of the encoder.
	void FVideoEncoderRTC::SetRates(RateControlParameters const& parameters)
	{
		PendingRateChange.Emplace(parameters);
	}

	webrtc::VideoEncoder::EncoderInfo FVideoEncoderRTC::GetEncoderInfo() const
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

	void FVideoEncoderRTC::UpdateConfig()
	{
		if (FVideoEncoderH264Wrapper* Encoder = Factory.GetHardwareEncoder())
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

			NewConfig = CreateEncoderConfigFromCVars(NewConfig);

			Encoder->SetConfig(NewConfig);
		}
	}

	AVEncoder::FVideoEncoder::FLayerConfig FVideoEncoderRTC::CreateEncoderConfigFromCVars(AVEncoder::FVideoEncoder::FLayerConfig InEncoderConfig) const
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

	void FVideoEncoderRTC::SendEncodedImage(webrtc::EncodedImage const& encoded_image, webrtc::CodecSpecificInfo const* codec_specific_info
#if WEBRTC_VERSION == 84
		, webrtc::RTPFragmentationHeader const* fragmentation
#endif
	)
	{
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
			OnEncodedImageCallback->OnEncodedImage(encoded_image, codec_specific_info
#if WEBRTC_VERSION == 84
				, fragmentation
#endif
			);
		}
	}
} // namespace UE::PixelStreaming