// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "VideoCapturer.h"
#include "RawFrameBuffer.h"
#include "Codecs/PixelStreamingBaseVideoEncoder.h"
#include "Utils.h"

#include "HAL/PlatformTime.h"
#include "Misc/Timespan.h"

FVideoCapturer::FVideoCapturer(FPixelStreamingBaseVideoEncoder& InHWEncoder): HWEncoder(InHWEncoder)
{
	set_enable_video_adapter(false);

	std::vector<cricket::VideoFormat> Formats;
	Formats.push_back(cricket::VideoFormat(Width, Height, cricket::VideoFormat::FpsToInterval(Framerate), cricket::FOURCC_H264));
	SetSupportedFormats(Formats);
}

cricket::CaptureState FVideoCapturer::Start(const cricket::VideoFormat& Format)
{
	return cricket::CS_RUNNING;
}

void FVideoCapturer::OnFrameReady(const FTexture2DRHIRef& FrameBuffer)
{
	int64 TimestampUs = rtc::TimeMicros();

	FBufferId BufferId;
	if (!HWEncoder.CopyBackBuffer(FrameBuffer, FTimespan::FromMicroseconds(TimestampUs), BufferId))
	{
		return;
	}

	rtc::scoped_refptr<FRawFrameBuffer> Buffer = new rtc::RefCountedObject<FRawFrameBuffer>(MakeShared<FFrameDropDetector>(HWEncoder, BufferId), FrameBuffer->GetSizeX(), FrameBuffer->GetSizeY());

	webrtc::VideoFrame Frame = webrtc::VideoFrame::Builder().
		set_video_frame_buffer(Buffer).
		set_timestamp_us(TimestampUs).
		build();

	UE_LOG(PixelStreamer, VeryVerbose, TEXT("(%d) captured video %lld"), RtcTimeMs(), TimestampUs);
	OnFrame(Frame, Buffer->width(), Buffer->height());
}
