// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WebRTCIncludes.h"

class FPixelStreamingFrameSource;
class FPixelStreamingLayerFrameSource;

class FPixelStreamingSimulcastFrameBuffer : public webrtc::VideoFrameBuffer
{
public:
	FPixelStreamingSimulcastFrameBuffer(FPixelStreamingFrameSource* InFrameSource);

	virtual ~FPixelStreamingSimulcastFrameBuffer();

	FPixelStreamingLayerFrameSource* GetLayerFrameSource(int LayerIndex) const;

    // Begin webrtc::VideoFrameBuffer interface
    virtual webrtc::VideoFrameBuffer::Type type() const override
    {
        return webrtc::VideoFrameBuffer::Type::kNative;
    }

	virtual int width() const override;
    virtual int height() const override;

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

private:
	FPixelStreamingFrameSource* FrameSource = nullptr;
};

class FPixelStreamingLayerFrameBuffer : public webrtc::VideoFrameBuffer
{
public:
	FPixelStreamingLayerFrameBuffer(FPixelStreamingLayerFrameSource* InLayerFrameSource);

	virtual ~FPixelStreamingLayerFrameBuffer();

	FTexture2DRHIRef GetFrame() const;

    // Begin webrtc::VideoFrameBuffer interface
    virtual webrtc::VideoFrameBuffer::Type type() const override
    {
        return webrtc::VideoFrameBuffer::Type::kNative;
    }

	virtual int width() const override;
    virtual int height() const override;

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

private:
	FPixelStreamingLayerFrameSource* LayerFrameSource = nullptr;
};
