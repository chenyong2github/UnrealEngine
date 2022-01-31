// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Templates/SharedPointer.h"
#include "Templates/RefCounting.h"
#include "Async/Async.h"
#include "RHI.h"
#include "HAL/ThreadSafeBool.h"
#include "Rendering/SlateRenderer.h"
#include "GPUFencePoller.h"
#include "Framework/Application/SlateApplication.h"
#include "Stats.h"

namespace UE
{
	namespace PixelStreaming
	{
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
			virtual FString& GetName() const = 0;
		};

		/*
		* Struct with `FTexture2DRHIRef` and the pixels of that texture read into a `TArray<FColor>`.
		*/
		struct FRawPixelsTexture : public FRefCountBase
		{
			FTexture2DRHIRef TextureRef;
			TArray<FColor> RawPixels;
			FRawPixelsTexture(FTexture2DRHIRef TexRef)
				: TextureRef(TexRef){};
		};

		/*
        * Base class for TextureSources that get their textures from the UE backbuffer.
		* Textures are copied from the backbuffer using a triplebuffering mechanism so that texture read access is always thread safe while writes are occurring.
		* If no texture has been written since the last read then the same texture will be read again.
        * This class also has the additional functionality of scaling textures from the backbuffer.
        * Note: This is a template class that uses CRTP - see: https://en.wikipedia.org/wiki/Curiously_recurring_template_pattern
        * For derived types of this class they must contain the following static methods:
        * 
        * static void CopyTexture(const FTexture2DRHIRef& SourceTexture, TRefCountPtr<TextureType> DestTexture, FGPUFenceRHIRef& CopyFence)
        * static TRefCountPtr<TextureType> CreateTexture(int Width, int Height)
        * static FTexture2DRHIRef ToTextureRef(TRefCountPtr<TextureType> Texture)
		* static FString& GetName()
        */
		template <class TextureType, class DerivedType>
		class TTextureSourceBackBufferBase : public ITextureSource, public TSharedFromThis<TTextureSourceBackBufferBase<TextureType, DerivedType>, ESPMode::ThreadSafe>
		{
		public:
			TTextureSourceBackBufferBase(float InFrameScale)
				: FrameScale(InFrameScale)
			{
				// Explictly make clear we are adding another ref to this shared bool for the purposes of using in the below lambda
				TSharedRef<bool, ESPMode::ThreadSafe> bEnabledClone = bEnabled;

				// The backbuffer delegate can only be accessed using GameThread
				AsyncTask(ENamedThreads::GameThread, [this, bEnabledClone]() {
					/*Early exit if `this` died before game thread ran.*/
					if (bEnabledClone.IsUnique())
					{
						return;
					}
					OnBackbuffer = &FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent();
					BackbufferDelegateHandle = OnBackbuffer->AddSP(this, &TTextureSourceBackBufferBase::OnBackBufferReady_RenderThread);
				});
			}

			TTextureSourceBackBufferBase()
				: TTextureSourceBackBufferBase(1.0)
			{
			}

			virtual ~TTextureSourceBackBufferBase()
			{
				if (OnBackbuffer)
				{
					OnBackbuffer->Remove(BackbufferDelegateHandle);
				}

				*bEnabled = false;
			}

			TRefCountPtr<TextureType> GetCurrent()
			{
				if (bIsTempDirty)
				{
					FScopeLock Lock(&CriticalSection);
					ReadBuffer.Swap(TempBuffer);
					bIsTempDirty = false;
				}
				return ReadBuffer;
			}

			/* Begin ITextureSource interface */
			virtual void SetEnabled(bool bInEnabled) override
			{
				*bEnabled = bInEnabled;
				// This source has been disabled, so set `bInitialized` to false so `OnBackBufferReady_RenderThread`
				// will make new textures next time it is called.
				if (bInitialized && bInEnabled == false)
				{
					bInitialized = false;
				}
			}

			virtual FTexture2DRHIRef GetTexture() override
			{
				return DerivedType::ToTextureRef(GetCurrent());
			}

			bool IsEnabled() const override { return *bEnabled; }
			bool IsAvailable() const override { return bInitialized; }
			int GetSourceWidth() const override { return SourceWidth; }
			int GetSourceHeight() const override { return SourceHeight; }
			FString& GetName() const override { return DerivedType::GetNameImpl(); };
			/* End ITextureSource interface */

			float GetFrameScaling() const { return FrameScale; }

		private:
			void OnBackBufferReady_RenderThread(SWindow& SlateWindow, const FTexture2DRHIRef& FrameBuffer)
			{
				if (!bInitialized)
				{
					Initialize(FrameBuffer->GetSizeXY().X * FrameScale, FrameBuffer->GetSizeXY().Y * FrameScale);
				}

				if (!IsEnabled())
				{
					return;
				}

				auto& WriteBuffer = bWriteParity ? WriteBuffers[0] : WriteBuffers[1];
				bWriteParity = !bWriteParity;

				// for safety we just make sure that the buffer is not currently waiting for a copy
				if (WriteBuffer.bAvailable)
				{
					WriteBuffer.bAvailable = false;

					FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
					WriteBuffer.Fence->Clear();

					RHICmdList.EnqueueLambda([&WriteBuffer](FRHICommandListImmediate& RHICmdList) {
						WriteBuffer.PreWaitingOnCopy = FPlatformTime::Cycles64();
					});

					DerivedType::CopyTexture(FrameBuffer, WriteBuffer.Texture, WriteBuffer.Fence);

					UE::PixelStreaming::FGPUFencePoller::Get()->AddJob(WriteBuffer.Fence, bEnabled, [this, &WriteBuffer]() {
						// This lambda is called only once the GPUFence is done
						{
							FScopeLock Lock(&CriticalSection);
							TempBuffer.Swap(WriteBuffer.Texture);
							WriteBuffer.Fence->Clear();
							WriteBuffer.bAvailable = true;

							bIsTempDirty = true;
						}

						// For debugging timing information about the copy operation
						// Turning it on all the time is a bit too much log spam if logging stats
						uint64 PostWaitingOnCopy = FPlatformTime::Cycles64();
						UE::PixelStreaming::FStats* Stats = UE::PixelStreaming::FStats::Get();
						if (Stats)
						{
							double CaptureLatencyMs = FPlatformTime::ToMilliseconds64(PostWaitingOnCopy - WriteBuffer.PreWaitingOnCopy);
							Stats->StoreApplicationStat(UE::PixelStreaming::FStatData(FName(*FString::Printf(TEXT("Layer (x%.2f) Capture time (ms)"), FrameScale)), CaptureLatencyMs, 2, true));
						}
					});
				}
			}

			void Initialize(int Width, int Height)
			{
				SourceWidth = Width;
				SourceHeight = Height;

				for (auto& Buffer : WriteBuffers)
				{
					Buffer.Texture = DerivedType::CreateTexture(SourceWidth, SourceHeight);
					Buffer.Fence = GDynamicRHI->RHICreateGPUFence(TEXT("VideoCapturerCopyFence"));
					Buffer.bAvailable = true;
				}
				bWriteParity = true;

				TempBuffer = DerivedType::CreateTexture(SourceWidth, SourceHeight);
				ReadBuffer = DerivedType::CreateTexture(SourceWidth, SourceHeight);
				bIsTempDirty = false;

				bInitialized = true;
			}

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
				TRefCountPtr<TextureType> Texture;
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
			TRefCountPtr<TextureType> TempBuffer;
			TRefCountPtr<TextureType> ReadBuffer;
			FThreadSafeBool bIsTempDirty;
		};

		/*
		* Captures the backbuffer into a `FTexture2DRHIRef` in whatever pixel format the backbuffer is already using.
		*/
		class FTextureSourceBackBuffer : public TTextureSourceBackBufferBase<FRHITexture2D, FTextureSourceBackBuffer>
		{
		public:
			FTextureSourceBackBuffer();
			FTextureSourceBackBuffer(float InScale);

			/* Begin TTextureSourceBackBufferBase "template interface" */
			static void CopyTexture(const FTexture2DRHIRef& SourceTexture, TRefCountPtr<FRHITexture2D> DestTexture, FGPUFenceRHIRef& CopyFence);
			static TRefCountPtr<FRHITexture2D> CreateTexture(int Width, int Height);
			static FTexture2DRHIRef ToTextureRef(TRefCountPtr<FRHITexture2D> Texture);
			static FString& GetNameImpl() { return Name; }
			/* End TTextureSourceBackBufferBase "template interface" */
		private:
			inline static FString Name = FString(TEXT("FTextureSourceBackBuffer"));
		};

		/*
		* Captures the backbuffer into a `FTexture2DRHIRef` in whatever pixel format the backbuffer is already using AND
		* reads that texture to CPU memory (which is slow). 
		* Note: Our intent is to remove this in favour of doing all swizzling on the GPU (CPU swizzling is the main purpose of reading the texture on the CPU currently).
		*/
		class FTextureSourceBackBufferToCPU : public TTextureSourceBackBufferBase<FRawPixelsTexture, FTextureSourceBackBufferToCPU>
		{
		public:
			FTextureSourceBackBufferToCPU();
			FTextureSourceBackBufferToCPU(float InScale);

			/* Begin TTextureSourceBackBufferBase "template interface" */
			static void CopyTexture(const FTexture2DRHIRef& SourceTexture, TRefCountPtr<FRawPixelsTexture> DestTexture, FGPUFenceRHIRef& CopyFence);
			static TRefCountPtr<FRawPixelsTexture> CreateTexture(int Width, int Height);
			static FTexture2DRHIRef ToTextureRef(TRefCountPtr<FRawPixelsTexture> Texture);
			static FString& GetNameImpl() { return Name; }
			/* End TTextureSourceBackBufferBase "template interface" */
		private:
			inline static FString Name = FString(TEXT("FTextureSourceBackBufferToCPU"));
		};

	} // namespace PixelStreaming
} // namespace UE
