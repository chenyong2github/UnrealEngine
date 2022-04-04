// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "TextureTripleBuffer.h"
#include "Stats.h"
#include "Utils.h"
#include "Settings.h"
#include "Async/Async.h"
#include "RHI.h"
#include "Rendering/SlateRenderer.h"
#include "Framework/Application/SlateApplication.h"
#include "IPixelStreamingModule.h"

namespace
{
	template <typename T>
	void Swap(TSharedPtr<T>& PtrA, TSharedPtr<T>& PtrB)
	{
		TSharedPtr<T> Temp = MoveTemp(PtrA);
		PtrA = MoveTemp(PtrB);
		PtrB = MoveTemp(Temp);
	}
} // namespace

namespace UE::PixelStreaming
{
	FTextureTripleBuffer::FTextureTripleBuffer(float InFrameScale, TUniquePtr<FPixelStreamingTextureSource> InTextureGenerator)
		: FrameScale(InFrameScale)
		, TextureGenerator(MoveTemp(InTextureGenerator))
	{
		TextureGenerator->OnNewTexture.AddRaw(this, &FTextureTripleBuffer::OnNewTexture);
	}

	FTextureTripleBuffer::~FTextureTripleBuffer()
	{
		*bEnabled = false;
	}

	TSharedPtr<FPixelStreamingTextureWrapper> FTextureTripleBuffer::GetCurrent()
	{
		if (bIsTempDirty)
		{
			FScopeLock Lock(&CriticalSection);
			Swap(ReadBuffer, TempBuffer);
			bIsTempDirty = false;
		}
		return ReadBuffer;
	}

	void FTextureTripleBuffer::SetEnabled(bool bInEnabled)
	{
		*bEnabled = bInEnabled;
		// This source has been disabled, so set `bInitialized` to false so `OnNewTexture`
		// will make new textures next time it is called.
		if (bInitialized && bInEnabled == false)
		{
			bInitialized = false;
		}
	}

	void FTextureTripleBuffer::Initialize(uint32 Width, uint32 Height)
	{
		SourceWidth = Width;
		SourceHeight = Height;

		WriteBuffer1 = CreateWriteBuffer(SourceWidth, SourceHeight);
		WriteBuffer2 = CreateWriteBuffer(SourceWidth, SourceHeight);
		bWriteParity = true;

		TempBuffer = TextureGenerator->CreateBlankStagingTexture(SourceWidth, SourceHeight);
		ReadBuffer = TextureGenerator->CreateBlankStagingTexture(SourceWidth, SourceHeight);
		bIsTempDirty = false;

		bInitialized = true;
	}

	void FTextureTripleBuffer::OnNewTexture(FPixelStreamingTextureWrapper& NewFrame, uint32 FrameWidth, uint32 FrameHeight)
	{
		// Note: this is the logic that makes mid-stream resolution changing work.
		bool bFrameSizeMismatch = (FrameWidth * FrameScale != SourceWidth) || (FrameHeight * FrameScale != SourceHeight);

		if (!bInitialized || bFrameSizeMismatch)
		{
			Initialize(FrameWidth * FrameScale, FrameHeight * FrameScale);
		}

		if (!IsEnabled())
		{
			return;
		}

		TSharedPtr<FWriteBuffer> WriteBuffer = bWriteParity ? WriteBuffer1 : WriteBuffer2;
		bWriteParity = !bWriteParity;

		// for safety we just make sure that the buffer is not currently waiting for a copy
		if (WriteBuffer->bAvailable)
		{
			WriteBuffer->bAvailable = false;

			if (IsInRenderingThread())
			{
				FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
				RHICmdList.EnqueueLambda([WriteBuffer](FRHICommandListImmediate& RHICmdList) {
					if (WriteBuffer.IsValid())
					{
						WriteBuffer->PreWaitingOnCopy = FPlatformTime::Cycles64();
					}
				});
			}
			else
			{
				WriteBuffer->PreWaitingOnCopy = FPlatformTime::Cycles64();
			}

			WriteBuffer->FrameCapturer->CaptureTexture(NewFrame, WriteBuffer->CapturedTexture);

			IPixelStreamingModule::Get().AddPollerTask(
				[this, WriteBuffer]() {
					WriteBuffer->FrameCapturer->OnCaptureFinished(WriteBuffer->CapturedTexture);
					// This lambda is called only once the GPUFence is done
					{
						FScopeLock Lock(&CriticalSection);
						Swap(TempBuffer, WriteBuffer->CapturedTexture);
						WriteBuffer->bAvailable = true;
						bIsTempDirty = true;
					}

					// Capture timing information about the copy operation
					uint64 PostWaitingOnCopy = FPlatformTime::Cycles64();
					UE::PixelStreaming::FStats* Stats = UE::PixelStreaming::FStats::Get();
					if (Stats)
					{
						double CaptureLatencyMs = FPlatformTime::ToMilliseconds64(PostWaitingOnCopy - WriteBuffer->PreWaitingOnCopy);
						Stats->StoreApplicationStat(UE::PixelStreaming::FStatData(FName(*FString::Printf(TEXT("Layer (x%.2f) Capture time (ms)"), FrameScale)), CaptureLatencyMs, 2, true));
					}
				},
				[WriteBuffer]() -> bool {
					return WriteBuffer.IsValid() ? WriteBuffer->FrameCapturer->IsCaptureFinished() : false;
				},
				bEnabled);
		}
	}

	TSharedPtr<FWriteBuffer> FTextureTripleBuffer::CreateWriteBuffer(uint32 Width, uint32 Height)
	{
		TSharedPtr<FWriteBuffer> Buffer = MakeShared<FWriteBuffer>();
		Buffer->bAvailable = true;
		Buffer->FrameCapturer = TextureGenerator->CreateFrameCapturer();
		Buffer->CapturedTexture = TextureGenerator->CreateBlankStagingTexture(Width, Height);
		return Buffer;
	}
} // namespace UE::PixelStreaming