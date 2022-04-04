// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "PixelStreamingFrameBuffer.h"
#include "TextureTripleBuffer.h"

namespace UE::PixelStreaming
{
	/*
	* ----------------- FInitializeFrameBuffer -----------------
	* We use this frame to force webrtc to create encoders that will siphon off other streams
	* but never get real frames directly pumped to them.
	*/
	class FInitializeFrameBuffer : public FPixelStreamingFrameBuffer
	{
	public:
		FInitializeFrameBuffer(TSharedPtr<FTextureTripleBuffer> InTextureBuffer);
		virtual ~FInitializeFrameBuffer();

		virtual EPixelStreamingFrameBufferType GetFrameBufferType() const { return EPixelStreamingFrameBufferType::Initialize; }

		virtual int width() const override;
		virtual int height() const override;

	private:
		TSharedPtr<FTextureTripleBuffer> TextureBuffer;
	};

	/*
	* ----------------- FSimulcastFrameBuffer -----------------
	* Holds a number of textures (called layers) and each is assigned an index.
	*/
	class FSimulcastFrameBuffer : public FPixelStreamingFrameBuffer
	{
	public:
		FSimulcastFrameBuffer(TArray<TSharedPtr<FTextureTripleBuffer>>& InTextureBuffer);
		virtual ~FSimulcastFrameBuffer();

		virtual EPixelStreamingFrameBufferType GetFrameBufferType() const { return EPixelStreamingFrameBufferType::Simulcast; }
		TSharedPtr<FTextureTripleBuffer> GetLayerFrameSource(int LayerIndex) const;
		int GetNumLayers() const;

		virtual int width() const override;
		virtual int height() const override;

	private:
		TArray<TSharedPtr<FTextureTripleBuffer>>& TextureBuffers;
	};

	/*
	* ----------------- FLayerFrameBuffer -----------------
	* Holds a genuine single texture for encoding.
	*/
	class FLayerFrameBuffer : public FPixelStreamingFrameBuffer
	{
	public:
		FLayerFrameBuffer(TSharedPtr<FTextureTripleBuffer> InTextureBuffer);
		virtual ~FLayerFrameBuffer();

		virtual EPixelStreamingFrameBufferType GetFrameBufferType() const { return EPixelStreamingFrameBufferType::Layer; }
		TSharedPtr<FPixelStreamingTextureWrapper> GetFrame() const;

		virtual int width() const override;
		virtual int height() const override;

	private:
		TSharedPtr<FTextureTripleBuffer> TextureBuffer;
	};

	/*
	* ----------------- FFrameBufferI420 -----------------
	* Holds a texture source that is capable of being converted to I420 for encoding.
	*/
	class FFrameBufferI420 : public FPixelStreamingFrameBuffer
	{
	public:
		FFrameBufferI420(TSharedPtr<FTextureTripleBuffer> InTextureBuffer);
		virtual ~FFrameBufferI420();

		virtual EPixelStreamingFrameBufferType GetFrameBufferType() const { return EPixelStreamingFrameBufferType::Layer; }
		TSharedPtr<FPixelStreamingTextureWrapper> GetFrame() const;

		virtual int width() const override;
		virtual int height() const override;
		virtual rtc::scoped_refptr<webrtc::I420BufferInterface> ToI420() override;
		virtual const webrtc::I420BufferInterface* GetI420() const override;

	private:
		TSharedPtr<FTextureTripleBuffer> TextureBuffer;
		rtc::scoped_refptr<webrtc::I420Buffer> Buffer;
	};
} // namespace UE::PixelStreaming