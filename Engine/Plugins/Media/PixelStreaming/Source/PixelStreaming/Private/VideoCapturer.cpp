// Copyright Epic Games, Inc. All Rights Reserved.

#include "VideoCapturer.h"
#include "RawFrameBuffer.h"
#include "Utils.h"

#include "HAL/PlatformTime.h"
#include "Misc/Timespan.h"

extern TAutoConsoleVariable<int32> CVarPixelStreamingEncoderUseBackBufferSize;
extern TAutoConsoleVariable<FString> CVarPixelStreamingEncoderTargetSize;

FVideoCapturer::FVideoCapturer(FHWEncoderDetails& InHWEncoderDetails): HWEncoderDetails(InHWEncoderDetails)
{
	set_enable_video_adapter(false);

	std::vector<cricket::VideoFormat> Formats;
	Formats.push_back(cricket::VideoFormat(Width, Height, cricket::VideoFormat::FpsToInterval(Framerate), cricket::FOURCC_H264));
	SetSupportedFormats(Formats);

	LastTimestampUs = rtc::TimeMicros();
}

cricket::CaptureState FVideoCapturer::Start(const cricket::VideoFormat& Format)
{
	return cricket::CS_RUNNING;
}

void FVideoCapturer::OnFrameReady(const FTexture2DRHIRef& FrameBuffer)
{
	int64 TimestampUs = rtc::TimeMicros();

	FIntPoint Resolution = FrameBuffer->GetSizeXY();
	if (CVarPixelStreamingEncoderUseBackBufferSize.GetValueOnRenderThread() == 0)
	{
		FString EncoderTargetSize = CVarPixelStreamingEncoderTargetSize.GetValueOnRenderThread();
		FString TargetWidth, TargetHeight;
		bool bValidSize = EncoderTargetSize.Split(TEXT("x"), &TargetWidth, &TargetHeight);
		if (bValidSize)
		{
			Resolution.X = FCString::Atoi(*TargetWidth);
			Resolution.Y = FCString::Atoi(*TargetHeight);
		}
		else
		{
			UE_LOG(PixelStreamer, Error, TEXT("CVarPixelStreamingEncoderTargetSize is not in a valid format: %s. It should be e.g: \"1920x1080\""), *EncoderTargetSize);
			CVarPixelStreamingEncoderTargetSize->Set(*FString::Printf(TEXT("%dx%d"), Resolution.X, Resolution.Y));
		}
	}

	AVEncoder::FBufferId BufferId;
	if (!HWEncoderDetails.Encoder->CopyTexture(
		FrameBuffer,
		FTimespan::FromMicroseconds(TimestampUs),
		FTimespan::FromMicroseconds(TimestampUs - LastTimestampUs),
		BufferId,
		Resolution))
	{
		return;
	}

	LastTimestampUs = TimestampUs;

	rtc::scoped_refptr<FRawFrameBuffer> Buffer = new rtc::RefCountedObject<FRawFrameBuffer>(MakeShared<FFrameDropDetector>(*HWEncoderDetails.Encoder, BufferId), FrameBuffer->GetSizeX(), FrameBuffer->GetSizeY());

	webrtc::VideoFrame Frame = webrtc::VideoFrame::Builder().
		set_video_frame_buffer(Buffer).
		set_timestamp_us(TimestampUs).
		build();

	UE_LOG(PixelStreamer, VeryVerbose, TEXT("(%d) captured video %lld"), RtcTimeMs(), TimestampUs);
	OnFrame(Frame, Buffer->width(), Buffer->height());
}

