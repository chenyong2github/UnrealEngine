// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingPrivate.h"

#include "IMediaTextureSample.h"

#include "Templates/SharedPointer.h"
#include "Misc/AssertionMacros.h"

using FTextureSampleRef = TSharedRef<IMediaTextureSample, ESPMode::ThreadSafe>;

// implementation of "native" (GPU texture) `webrtc::VideoFrameBuffer`
class FVideoFrameBuffer : public webrtc::VideoFrameBuffer
{
public:
	explicit FVideoFrameBuffer(const FTextureSampleRef& InSample)
		: Sample(InSample)
	{}

	// ~BEGIN `webrtc::VideoFrameBuffer` impl
	Type type() const override
	{
		return Type::kNative;
	}

	int width() const override
	{
		return Sample->GetDim().X;
	}

	int height() const override
	{
		return Sample->GetDim().Y;
	}

	rtc::scoped_refptr<webrtc::I420BufferInterface> ToI420() override
	{
		unimplemented();
		return nullptr;
	}
	// ~END `webrtc::VideoFrameBuffer` impl

	const FTextureSampleRef& GetSample() const
	{
		return Sample;
	}

private:
	FTextureSampleRef Sample;
};

class FVideoSink : public rtc::VideoSinkInterface<webrtc::VideoFrame>
{
public:
	using FDelegate = TUniqueFunction<void(const FTextureSampleRef&)>;

	explicit FVideoSink(FDelegate&& InDelegate)
		: Delegate(MoveTemp(InDelegate))
	{}

	void OnFrame(const webrtc::VideoFrame& Frame) override
	{
		const FVideoFrameBuffer& FrameBuffer = static_cast<const FVideoFrameBuffer&>(*Frame.video_frame_buffer());
		Delegate(FrameBuffer.GetSample());
	}

private:
	FDelegate Delegate;
};
