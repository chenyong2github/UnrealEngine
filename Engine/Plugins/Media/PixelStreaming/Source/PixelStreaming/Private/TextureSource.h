// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "RHI.h"
#include "HAL/ThreadSafeBool.h"
#include "Rendering/SlateRenderer.h"
#include "Templates/SharedPointer.h"

namespace UE {
	namespace PixelStreaming {

		/*
		* Interface for all texture sources in Pixel Streaming.
		* These texture sources are used to populate video sources for video tracks.
		*/
		class ITextureSource
		{
			public:
				ITextureSource() = default;
				virtual ~ITextureSource() = default;
				virtual bool IsAvailable() const = 0;
				virtual bool IsEnabled() const = 0;
				virtual void SetEnabled(bool bInEnabled) = 0;
				virtual int GetSourceHeight() const = 0;
				virtual int GetSourceWidth() const = 0;
				virtual FTexture2DRHIRef GetTexture() = 0;
		};


		/*
		* Source of textures coming from the UE backbuffer.
		* This class has the additional functionality of scaling textures from the backbuffer.
		*/
		class FTextureSourceBackBuffer : public ITextureSource, public TSharedFromThis<FTextureSourceBackBuffer, ESPMode::ThreadSafe>
		{
		public:
			FTextureSourceBackBuffer();
			FTextureSourceBackBuffer(float InScale);
			~FTextureSourceBackBuffer();
			
			/* Begin ITextureSource interface */
			void SetEnabled(bool bInEnabled) override;
			bool IsEnabled() const override { return *bEnabled; }
			bool IsAvailable() const override { return bInitialized; }
			int GetSourceWidth() const override { return SourceWidth; }
			int GetSourceHeight() const override { return SourceHeight; }
			FTexture2DRHIRef GetTexture() override;
			/* End ITextureSource interface */

			float GetFrameScaling() const { return FrameScale; }

		private:
			void OnBackBufferReady_RenderThread(SWindow& SlateWindow, const FTexture2DRHIRef& FrameBuffer);
			void Initialize(int Width, int Height);

		private:
			const float FrameScale;
			int SourceWidth = 0;
			int SourceHeight = 0;
			FThreadSafeBool bInitialized = false;
			TSharedRef<bool, ESPMode::ThreadSafe> bEnabled = MakeShared<bool, ESPMode::ThreadSafe>(true);
			FSlateRenderer::FOnBackBufferReadyToPresent* OnBackbuffer = nullptr;
			FDelegateHandle BackbufferDelegateHandle;
			FCriticalSection DestructorCriticalSection;

			struct FCaptureFrame
			{
				FTexture2DRHIRef Texture;
				FGPUFenceRHIRef Fence;
				bool bAvailable = true;
				uint64 PreWaitingOnCopy;
			};

			/*
			* Triple buffer setup with queued write buffers (since we have to wait for RHI copy).
			* 1 Read buffer (read the captured texture)
			* 1 Temp buffer (for swapping what is read and written)
			* 2 Write buffers (2 write buffers because UE can render two frames before presenting sometimes)
			*/
			FCriticalSection CriticalSection;
			bool bWriteParity = true;
			FCaptureFrame WriteBuffers[2];
			FTexture2DRHIRef TempBuffer;
			FTexture2DRHIRef ReadBuffer;
			FThreadSafeBool bIsTempDirty;
		};

	}
}
