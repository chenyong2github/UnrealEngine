// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RHI.h"
#include "WebRTCIncludes.h"
#include "TextureSource.h"

namespace UE
{
	namespace PixelStreaming
	{

		enum FFrameBufferType
		{
			Initialize,
			Simulcast,
			Layer
		};

		/*
		* ----------------- FFrameBuffer -----------------
		* The base framebuffer that extends the WebRTC type.
		*/
		class FFrameBuffer : public webrtc::VideoFrameBuffer
		{
		public:
			virtual ~FFrameBuffer() {}

			virtual FFrameBufferType GetFrameBufferType() const = 0;

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
		class FInitializeFrameBuffer : public FFrameBuffer
		{
		public:
			FInitializeFrameBuffer(TSharedPtr<ITextureSource> InTextureSource);
			virtual ~FInitializeFrameBuffer();

			virtual FFrameBufferType GetFrameBufferType() const { return Initialize; }

			virtual int width() const override;
			virtual int height() const override;

		private:
			TSharedPtr<ITextureSource> TextureSource;
		};

		/*
		* ----------------- FSimulcastFrameBuffer -----------------
		* Holds a number of textures (called layers) and each is assigned an index.
		*/
		class FSimulcastFrameBuffer : public FFrameBuffer
		{
		public:
			FSimulcastFrameBuffer(TArray<TSharedPtr<ITextureSource>>& InTextureSources);
			virtual ~FSimulcastFrameBuffer();

			virtual FFrameBufferType GetFrameBufferType() const { return Simulcast; }
			TSharedPtr<ITextureSource> GetLayerFrameSource(int LayerIndex) const;
			int GetNumLayers() const;

			virtual int width() const override;
			virtual int height() const override;

		private:
			TArray<TSharedPtr<ITextureSource>>& TextureSources;
		};

		/*
		* ----------------- FLayerFrameBuffer -----------------
		* Holds a genuine single texture for encoding.
		*/
		class FLayerFrameBuffer : public FFrameBuffer
		{
		public:
			FLayerFrameBuffer(TSharedPtr<ITextureSource> InTextureSource);
			virtual ~FLayerFrameBuffer();

			virtual FFrameBufferType GetFrameBufferType() const { return Layer; }
			FTexture2DRHIRef GetFrame() const;

			virtual int width() const override;
			virtual int height() const override;

		private:
			TSharedPtr<ITextureSource> TextureSource;
		};

		/*
		* ----------------- FFrameBufferI420 -----------------
		* Holds a texture source that is capable of being converted to I420 for encoding.
		*/
		class FFrameBufferI420 : public FFrameBuffer
		{
		public:
			FFrameBufferI420(TSharedPtr<ITextureSource> InTextureSource);
			virtual ~FFrameBufferI420();

			virtual FFrameBufferType GetFrameBufferType() const { return Layer; }
			FTexture2DRHIRef GetFrame() const;

			virtual int width() const override;
			virtual int height() const override;
			virtual rtc::scoped_refptr<webrtc::I420BufferInterface> ToI420() override;
			virtual const webrtc::I420BufferInterface* GetI420() const override;

		private:
			TSharedPtr<ITextureSource> TextureSource;
			rtc::scoped_refptr<webrtc::I420Buffer> Buffer;
		};

	} // namespace PixelStreaming
} // namespace UE
