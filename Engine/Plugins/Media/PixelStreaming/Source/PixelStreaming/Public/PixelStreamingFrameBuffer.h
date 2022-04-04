// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/RefCounting.h"
#include "Templates/SharedPointer.h"
#include "api/video/video_frame_buffer.h"
#include "api/video/i420_buffer.h"
#include "api/scoped_refptr.h"

enum EPixelStreamingFrameBufferType
{
	Initialize,
	Simulcast,
	Layer
};

/*
 * ----------------- FPixelStreamingFrameBuffer -----------------
 * The base framebuffer that extends the WebRTC type.
 */
class PIXELSTREAMING_API FPixelStreamingFrameBuffer : public webrtc::VideoFrameBuffer
{
public:
	virtual ~FPixelStreamingFrameBuffer() {}

	virtual EPixelStreamingFrameBufferType GetFrameBufferType() const = 0;

	// Begin webrtc::VideoFrameBuffer interface
	virtual webrtc::VideoFrameBuffer::Type type() const override
	{
		return webrtc::VideoFrameBuffer::Type::kNative;
	}

	virtual int width() const = 0;
	virtual int height() const = 0;

	virtual rtc::scoped_refptr<webrtc::I420BufferInterface> ToI420() override
	{
		unimplemented();
		return nullptr;
	}

	virtual const webrtc::I420BufferInterface* GetI420() const override
	{
		unimplemented();
		return nullptr;
	}
	// End webrtc::VideoFrameBuffer interface
};
