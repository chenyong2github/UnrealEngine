// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingWebRTCIncludes.h"

enum EPixelStreamingFrameBufferType
{
	Initialize,
	Simulcast,
	Layer
};

/*
 * The base framebuffer that extends the WebRTC type.
 */
class PIXELSTREAMING_API IPixelStreamingFrameBuffer : public webrtc::VideoFrameBuffer
{
public:
	virtual ~IPixelStreamingFrameBuffer() = default;

	virtual EPixelStreamingFrameBufferType GetFrameBufferType() const = 0;

	// Begin webrtc::VideoFrameBuffer interface
	virtual webrtc::VideoFrameBuffer::Type type() const override { return webrtc::VideoFrameBuffer::Type::kNative; }

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
