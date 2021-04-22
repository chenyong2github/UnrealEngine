// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WebRTCIncludes.h"
#include "VideoEncoderInput.h"

class FPixelStreamingFrameBuffer : public webrtc::VideoFrameBuffer
{
public:
	explicit FPixelStreamingFrameBuffer(AVEncoder::FVideoEncoderInputFrame* InputFrame, TSharedPtr<AVEncoder::FVideoEncoderInput> InputVideoEncoderInput)
		: Frame(InputFrame), VideoEncoderInput(InputVideoEncoderInput)
	{
		Frame->Obtain();
	}

	~FPixelStreamingFrameBuffer()
	{
		Frame->Release();
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
		return Frame->GetWidth();
	}

	virtual int height() const override
	{
		return Frame->GetHeight();
	}

	// we should not hit this as we support a native_buffer
	rtc::scoped_refptr<webrtc::I420BufferInterface> ToI420() override
	{		
		const AVEncoder::FVideoEncoderInputFrame::FYUV420P& YUV420P = Frame->GetYUV420P();
		return webrtc::I420Buffer::Copy(
			Frame->GetWidth(),
			Frame->GetHeight(),
			YUV420P.Data[0],
			YUV420P.StrideY,
			YUV420P.Data[1],
			YUV420P.StrideU,
			YUV420P.Data[2],
			YUV420P.StrideV
		);
	}

	// HACK (M84FIX) Don't blame me this const_cast is from the reference implimentation
	AVEncoder::FVideoEncoderInputFrame* GetFrame() const
	{
		return Frame;
	}

	TSharedPtr<AVEncoder::FVideoEncoderInput> GetVideoEncoderInput() const
	{
		return VideoEncoderInput;
	}

private:
	AVEncoder::FVideoEncoderInputFrame* Frame;
	TSharedPtr<AVEncoder::FVideoEncoderInput> VideoEncoderInput;
};