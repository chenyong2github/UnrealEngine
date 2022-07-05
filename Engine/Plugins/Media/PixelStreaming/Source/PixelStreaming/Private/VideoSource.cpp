// Copyright Epic Games, Inc. All Rights Reserved.

#include "VideoSource.h"
#include "Settings.h"
#include "FrameBufferH264.h"
#include "FrameBufferI420.h"
#include "FrameBufferInitialize.h"
#include "FrameAdapter.h"

namespace UE::PixelStreaming
{
	FVideoSource::FVideoSource(TSharedPtr<IPixelStreamingVideoInput> InVideoInput, bool bInAllowSimulcast, const TFunction<bool()>& InShouldGenerateFramesCheck)
		: CurrentState(webrtc::MediaSourceInterface::SourceState::kInitializing)
		, VideoInput(InVideoInput)
		, bAllowSimulcast(bInAllowSimulcast)
		, ShouldGenerateFramesCheck(InShouldGenerateFramesCheck)
	{
		CreateFrameAdapter();
	}

	void FVideoSource::SetCoupleFramerate(bool bCouple)
	{
		if (bCoupleFramerate != bCouple)
		{
			bCoupleFramerate = bCouple;
			if (bCoupleFramerate)
			{
				AdaptCompleteHandle = FrameAdapter->OnComplete.AddRaw(this, &FVideoSource::OnAdaptComplete);
			}
			else
			{
				FrameAdapter->OnComplete.Remove(AdaptCompleteHandle);
			}
		}
	}

	void FVideoSource::InputFrame(const IPixelStreamingInputFrame& SourceFrame)
	{
		FScopeLock Lock(&FrameAdapterCS);
		if (FrameAdapter && ShouldGenerateFramesCheck())
		{
			FrameAdapter->Process(SourceFrame);
		}
	}

	void FVideoSource::ResolutionChanged(int32 NewWidth, int32 NewHeight)
	{
		// when the resolution changes we cant just alter the frame adapters since
		// there might be existing framebuffers in flight that are expecting the
		// adapters to be ready to read from. instead we stop our reference here
		// so any existing framebuffers will still have a valid adapter to read from
		// and once those frames are flushed the adapter should properly be destroyed.

		CreateFrameAdapter();
	}

	void FVideoSource::MaybePushFrame()
	{
		FScopeLock Lock(&FrameAdapterCS);
		if (FrameAdapter->IsReady())
		{
			PushFrame();
		}
	}

	void FVideoSource::PushFrame()
	{
		static int32 FrameId = 1;

		CurrentState = webrtc::MediaSourceInterface::SourceState::kLive;
		const int64 TimestampUs = rtc::TimeMicros();

		rtc::scoped_refptr<webrtc::VideoFrameBuffer> FrameBuffer;
		if (Settings::IsCodecVPX())
		{
			FrameBuffer = new rtc::RefCountedObject<FFrameBufferI420Simulcast>(FrameAdapter);
		}
		else
		{
			if (ShouldGenerateFramesCheck())
			{
				FrameBuffer = new rtc::RefCountedObject<FFrameBufferH264Simulcast>(FrameAdapter);
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
									   .set_id(FrameId++)
									   .build();
		OnFrame(Frame);
	}

	void FVideoSource::CreateFrameAdapter()
	{
		FScopeLock Lock(&FrameAdapterCS);

		TArray<float> LayerScaling;

		if (bAllowSimulcast)
		{
			for (auto& Layer : Settings::SimulcastParameters.Layers)
			{
				LayerScaling.Add(1.0f / Layer.Scaling);
			}
			LayerScaling.Sort([](float ScaleA, float ScaleB) { return ScaleA < ScaleB; });
		}
		else
		{
			LayerScaling.Add(1.0f);
		}

		FrameAdapter = FFrameAdapter::Create(VideoInput, LayerScaling);
		if (bCoupleFramerate)
		{
			AdaptCompleteHandle = FrameAdapter->OnComplete.AddRaw(this, &FVideoSource::OnAdaptComplete);
		}
	}

	void FVideoSource::OnAdaptComplete()
	{
		FScopeLock Lock(&FrameAdapterCS);
		PushFrame();
	}
} // namespace UE::PixelStreaming
