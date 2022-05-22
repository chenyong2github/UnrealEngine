// Copyright Epic Games, Inc. All Rights Reserved.

#include "VideoSourceP2P.h"
#include "Settings.h"
#include "FrameAdapterH264.h"
#include "FrameAdapterI420.h"
#include "FrameBufferH264.h"
#include "FrameBufferI420.h"
#include "FrameBufferInitialize.h"

namespace UE::PixelStreaming
{
	FVideoSourceP2P::FVideoSourceP2P(TSharedPtr<FPixelStreamingVideoInput> InVideoInput, TFunction<bool()> InIsQualityControllerFunc)
		: IsQualityControllerFunc(InIsQualityControllerFunc)
	{
		const float ScaleFactor = Settings::CVarPixelStreamingFrameScale.GetValueOnAnyThread();
		switch (Settings::GetSelectedCodec())
		{
			case Settings::ECodec::VP8:
			case Settings::ECodec::VP9:
				if (Settings::CVarPixelStreamingVPXUseCompute.GetValueOnAnyThread())
				{
					FrameAdapter = MakeShared<FFrameAdapterI420Compute>(InVideoInput, TArray<float>{ ScaleFactor });
				}
				else
				{
					FrameAdapter = MakeShared<FFrameAdapterI420CPU>(InVideoInput, TArray<float>{ ScaleFactor });
				}
				break;
			case Settings::ECodec::H264:
			default:
				FrameAdapter = MakeShared<FFrameAdapterH264>(InVideoInput, TArray<float>{ ScaleFactor });
				break;
		}
	}

	bool FVideoSourceP2P::IsReady() const
	{
		return FrameAdapter->IsReady();
	}

	webrtc::VideoFrame FVideoSourceP2P::CreateFrame(int32 FrameId)
	{
		const int64 TimestampUs = rtc::TimeMicros();

		// Based on quality control we either send a frame buffer with a texture or an empty frame
		// buffer to indicate that this frame should be skipped by the encoder as this peer is having its
		// frames transmitted by some other peer (the quality controlling peer).

		rtc::scoped_refptr<webrtc::VideoFrameBuffer> FrameBuffer;
		if (Settings::IsCodecVPX())
		{
			FrameBuffer = new rtc::RefCountedObject<FFrameBufferI420>(FrameAdapter, FrameAdapter->GetNumLayers() - 1);
		}
		else
		{
			if (IsQualityControllerFunc())
			{
				FrameBuffer = new rtc::RefCountedObject<FFrameBufferH264>(FrameAdapter);
			}
			else
			{
				FrameBuffer = new rtc::RefCountedObject<FFrameBufferInitialize>(FrameAdapter);
			}
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
