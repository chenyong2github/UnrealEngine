// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RHI.h"
#include "WebRTCIncludes.h"
#include "RHI.h"

class FPixelStreamingFrameSource;
class FPixelStreamingLayerFrameSource;

enum FPixelStreamingFrameBufferType
{
	Initialize,
	Simulcast,
	Layer
};

class FPixelStreamingFrameBuffer : public webrtc::VideoFrameBuffer
{
public:
	virtual ~FPixelStreamingFrameBuffer() {}

	virtual FPixelStreamingFrameBufferType GetFrameBufferType() const = 0;

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

// We use this frame to force webrtc to create encoders that will siphon off other streams
// but never get frames directly pumped to them.
class FPixelStreamingInitializeFrameBuffer : public FPixelStreamingFrameBuffer
{
public:
	FPixelStreamingInitializeFrameBuffer(FPixelStreamingFrameSource* InFrameSource);
	virtual ~FPixelStreamingInitializeFrameBuffer();

	virtual FPixelStreamingFrameBufferType GetFrameBufferType() const { return Initialize; }

	virtual int width() const override;
	virtual int height() const override;

private:
	FPixelStreamingFrameSource* FrameSource = nullptr;
};

class FPixelStreamingSimulcastFrameBuffer : public FPixelStreamingFrameBuffer
{
public:
	FPixelStreamingSimulcastFrameBuffer(FPixelStreamingFrameSource* InFrameSource);
	virtual ~FPixelStreamingSimulcastFrameBuffer();

	virtual FPixelStreamingFrameBufferType GetFrameBufferType() const { return Simulcast; }
	FPixelStreamingLayerFrameSource* GetLayerFrameSource(int LayerIndex) const;
	int GetNumLayers() const;

	virtual int width() const override;
	virtual int height() const override;

private:
	FPixelStreamingFrameSource* FrameSource = nullptr;
};

class FPixelStreamingLayerFrameBuffer : public FPixelStreamingFrameBuffer
{
public:
	FPixelStreamingLayerFrameBuffer(FPixelStreamingLayerFrameSource* InLayerFrameSource);
	virtual ~FPixelStreamingLayerFrameBuffer();

	virtual FPixelStreamingFrameBufferType GetFrameBufferType() const { return Layer; }
	FTexture2DRHIRef GetFrame() const;

	virtual int width() const override;
	virtual int height() const override;

private:
	FPixelStreamingLayerFrameSource* LayerFrameSource = nullptr;
};
