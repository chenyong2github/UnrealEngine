// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AVEncoder.h"

// WebRTC can drop frames in the encoder queue for various reasons, e.g. when more than one frame is waiting
// for encoding, or when encoder is not ready yet
// Our pipeline is asynchronous (cos we use NvEnc async encoding) so to keep track of captured frames
// we need to know when WebRTC drops frames.
// FFrameDropDetector is supposed to be used as a shared ptr to avoid multiple destructions on copying/moving.
// If frame is not dropped WebRTC will pass it to NvEnc and NvEnc should disable notification about frame drop.
// Otherwise `FFrameDropDetector` instance will be destroyed when WebRTC drops the associated frame 
// and so NvEnc will be notified immediately.
struct FFrameDropDetector final
{
	FFrameDropDetector(AVEncoder::FVideoEncoder& HWEncoder, AVEncoder::FBufferId BufferId):
		HWEncoder(&HWEncoder),
		BufferId(BufferId)
	{}

	~FFrameDropDetector()
	{
		if (HWEncoder)
			HWEncoder->Drop(BufferId);
	}

	AVEncoder::FVideoEncoder* HWEncoder = nullptr;
	AVEncoder::FBufferId BufferId;
};

class FRawFrameBuffer : public webrtc::VideoFrameBuffer
{
public:
	explicit FRawFrameBuffer(const TSharedPtr<FFrameDropDetector>& InFrameDropDetector, int InWidth, int InHeight):
		FrameDropDetector(InFrameDropDetector),
		Width(InWidth),
		Height(InHeight)
	{
	}

	//
	// webrtc::VideoFrameBuffer interface
	//
	Type type() const override
	{
		return Type::kNative;
	}

	virtual int width() const override
	{
		return Width;
	}

	virtual int height() const override
	{
		return Height;
	}

	rtc::scoped_refptr<webrtc::I420BufferInterface> ToI420() override
	{
		check(false);
		return nullptr;
	}

	//
	// Own methods
	//
	AVEncoder::FBufferId GetBuffer() const
	{
		return FrameDropDetector->BufferId;
	}

	void DisableFrameDropNotification()
	{
		FrameDropDetector->HWEncoder = nullptr;
	}

private:
	TSharedPtr<FFrameDropDetector> FrameDropDetector;
	int Width;
	int Height;
};

