// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VideoEncoderInput.h"
#include "FrameBuffer.h"

namespace UE {
	namespace PixelStreaming {
		/*
		* Factory to create `AVEncoder::FVideoEncoderInputFrame` for use in Pixel Streaming encoders.
		* This class is responsible for creating/managing the underlying `AVEncoder::FVideoEncoderInput` required to create
		* `AVEncoder::FVideoEncoderInputFrame`.
		* This class is not threadsafe and should only be accessed from a single thread.
		*/
		class FEncoderFrameFactory
		{
		public:
			FEncoderFrameFactory();
			~FEncoderFrameFactory();
			AVEncoder::FVideoEncoderInputFrame* GetFrameAndSetTexture(int InWidth, int InHeight, FTexture2DRHIRef InTexture);
			TSharedPtr<AVEncoder::FVideoEncoderInput> GetOrCreateVideoEncoderInput(int InWidth, int InHeight);
			void SetResolution(int InWidth, int InHeight);

		private:
			AVEncoder::FVideoEncoderInputFrame* GetOrCreateFrame(int InWidth, int InHeight, const FTexture2DRHIRef InTexture);
			void RemoveStaleTextures();
			void FlushFrames();
			TSharedPtr<AVEncoder::FVideoEncoderInput> CreateVideoEncoderInput(int InWidth, int InHeight) const;
			void SetTexture(AVEncoder::FVideoEncoderInputFrame* Frame, const FTexture2DRHIRef& Texture);
			void SetTextureCUDAVulkan(AVEncoder::FVideoEncoderInputFrame* Frame, const FTexture2DRHIRef& Texture);
		#if PLATFORM_WINDOWS
			void SetTextureCUDAD3D11(AVEncoder::FVideoEncoderInputFrame* Frame, const FTexture2DRHIRef& Texture);
			void SetTextureCUDAD3D12(AVEncoder::FVideoEncoderInputFrame* Frame, const FTexture2DRHIRef& Texture);
		#endif // PLATFORM_WINDOWS

		private:
			uint64 FrameId = 0;
			TSharedPtr<AVEncoder::FVideoEncoderInput> EncoderInput;
			// Store a mapping between raw textures and the FVideoEncoderInputFrames that wrap them
			TMap<FTexture2DRHIRef, AVEncoder::FVideoEncoderInputFrame*> TextureToFrameMapping;
		};
	}
}
