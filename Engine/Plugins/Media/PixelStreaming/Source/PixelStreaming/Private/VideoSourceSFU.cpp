// Copyright Epic Games, Inc. All Rights Reserved.

#include "VideoSourceSFU.h"
#include "Settings.h"
#include "FrameAdapterH264.h"
#include "FrameAdapterI420.h"
#include "FrameBufferH264.h"
#include "FrameBufferI420.h"

namespace UE::PixelStreaming
{
	FVideoSourceSFU::FVideoSourceSFU(TSharedPtr<FPixelStreamingVideoInput> InVideoInput)
	{
		TArray<float> LayerScaling;
		for (auto& Layer : Settings::SimulcastParameters.Layers)
		{
			LayerScaling.Add(1.0f / Layer.Scaling);
		}
		LayerScaling.Sort([](float ScaleA, float ScaleB) { return ScaleA < ScaleB; });

		switch (Settings::GetSelectedCodec())
		{
			case Settings::ECodec::VP8:
			case Settings::ECodec::VP9:
				if (Settings::CVarPixelStreamingVPXUseCompute.GetValueOnAnyThread())
				{
					FrameAdapter = MakeShared<FFrameAdapterI420Compute>(InVideoInput, LayerScaling);
				}
				else
				{
					FrameAdapter = MakeShared<FFrameAdapterI420CPU>(InVideoInput, LayerScaling);
				}
				break;
			case Settings::ECodec::H264:
			default:
				FrameAdapter = MakeShared<FFrameAdapterH264>(InVideoInput, LayerScaling);
				break;
		}
	}

	bool FVideoSourceSFU::IsReady() const
	{
		return FrameAdapter->IsReady();
	}

	webrtc::VideoFrame FVideoSourceSFU::CreateFrame(int32 FrameId)
	{
		const int64 TimestampUs = rtc::TimeMicros();

		rtc::scoped_refptr<webrtc::VideoFrameBuffer> FrameBuffer;
		if (Settings::IsCodecVPX())
		{
			// VPX Encoding. We only ever have a single layer
			// TODO if we change to use the simulcast adapter we want the simulcast frame buffer here
			FrameBuffer = new rtc::RefCountedObject<FFrameBufferI420Simulcast>(FrameAdapter);
		}
		else
		{
			FrameBuffer = new rtc::RefCountedObject<FFrameBufferH264Simulcast>(FrameAdapter);
		}

		webrtc::VideoFrame Frame = webrtc::VideoFrame::Builder()
									   .set_video_frame_buffer(FrameBuffer)
									   .set_timestamp_us(TimestampUs)
									   .set_rotation(webrtc::VideoRotation::kVideoRotation_0)
									   .set_id(FrameId)
									   .build();

		return Frame;
	}
} // namespace UE::PixelStreaming
