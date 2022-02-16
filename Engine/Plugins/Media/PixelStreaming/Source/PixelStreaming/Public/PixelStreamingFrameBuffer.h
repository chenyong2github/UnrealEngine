// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/RefCounting.h"
#include "Templates/SharedPointer.h"

#include "api/video/video_frame_buffer.h"
#include "api/video/i420_buffer.h"
#include "api/scoped_refptr.h"

class FRHITexture2D;
class IPixelStreamingTextureSource;

namespace UE
{
	namespace PixelStreaming
	{
		enum EFrameBufferType
		{
			Initialize,
			Simulcast,
			Layer
		};
	} // namespace PixelStreaming
} // namespace UE

/*
 * ----------------- FPixelStreamingFrameBuffer -----------------
 * The base framebuffer that extends the WebRTC type.
 */
class PIXELSTREAMING_API FPixelStreamingFrameBuffer : public webrtc::VideoFrameBuffer
{
public:
	virtual ~FPixelStreamingFrameBuffer() {}

	virtual UE::PixelStreaming::EFrameBufferType GetFrameBufferType() const = 0;

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

/*
 * ----------------- FInitializeFrameBuffer -----------------
 * We use this frame to force webrtc to create encoders that will siphon off other streams
 * but never get real frames directly pumped to them.
 */
class PIXELSTREAMING_API FInitializeFrameBuffer : public FPixelStreamingFrameBuffer
{
public:
	FInitializeFrameBuffer(TSharedPtr<IPixelStreamingTextureSource> InTextureSource);
	virtual ~FInitializeFrameBuffer();

	virtual UE::PixelStreaming::EFrameBufferType GetFrameBufferType() const { return UE::PixelStreaming::EFrameBufferType::Initialize; }

	virtual int width() const override;
	virtual int height() const override;

private:
	TSharedPtr<IPixelStreamingTextureSource> TextureSource;
};

/*
 * ----------------- FSimulcastFrameBuffer -----------------
 * Holds a number of textures (called layers) and each is assigned an index.
 */
class PIXELSTREAMING_API FSimulcastFrameBuffer : public FPixelStreamingFrameBuffer
{
public:
	FSimulcastFrameBuffer(TArray<TSharedPtr<IPixelStreamingTextureSource>>& InTextureSources);
	virtual ~FSimulcastFrameBuffer();

	virtual UE::PixelStreaming::EFrameBufferType GetFrameBufferType() const { return UE::PixelStreaming::EFrameBufferType::Simulcast; }
	TSharedPtr<IPixelStreamingTextureSource> GetLayerFrameSource(int LayerIndex) const;
	int GetNumLayers() const;

	virtual int width() const override;
	virtual int height() const override;

private:
	TArray<TSharedPtr<IPixelStreamingTextureSource>>& TextureSources;
};

/*
 * ----------------- FLayerFrameBuffer -----------------
 * Holds a genuine single texture for encoding.
 */
class PIXELSTREAMING_API FLayerFrameBuffer : public FPixelStreamingFrameBuffer
{
public:
	FLayerFrameBuffer(TSharedPtr<IPixelStreamingTextureSource> InTextureSource);
	virtual ~FLayerFrameBuffer();

	virtual UE::PixelStreaming::EFrameBufferType GetFrameBufferType() const { return UE::PixelStreaming::EFrameBufferType::Layer; }
	TRefCountPtr<FRHITexture2D> GetFrame() const;

	virtual int width() const override;
	virtual int height() const override;

private:
	TSharedPtr<IPixelStreamingTextureSource> TextureSource;
};

/*
 * ----------------- FFrameBufferI420 -----------------
 * Holds a texture source that is capable of being converted to I420 for encoding.
 */
class PIXELSTREAMING_API FFrameBufferI420 : public FPixelStreamingFrameBuffer
{
public:
	FFrameBufferI420(TSharedPtr<IPixelStreamingTextureSource> InTextureSource);
	virtual ~FFrameBufferI420();

	virtual UE::PixelStreaming::EFrameBufferType GetFrameBufferType() const { return UE::PixelStreaming::EFrameBufferType::Layer; }
	TRefCountPtr<FRHITexture2D> GetFrame() const;

	virtual int width() const override;
	virtual int height() const override;
	virtual rtc::scoped_refptr<webrtc::I420BufferInterface> ToI420() override;
	virtual const webrtc::I420BufferInterface* GetI420() const override;

private:
	TSharedPtr<IPixelStreamingTextureSource> TextureSource;
	rtc::scoped_refptr<webrtc::I420Buffer> Buffer;
};
