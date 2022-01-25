// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextureSource.h"
#include "Stats.h"
#include "Utils.h"
#include "Async/Async.h"
#include "GPUFencePoller.h"
#include "Framework/Application/SlateApplication.h"

/*
* -------- UE::PixelStreaming::FTextureSourceBackBuffer ----------------
*/

UE::PixelStreaming::FTextureSourceBackBuffer::FTextureSourceBackBuffer(float InFrameScale)
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
		BackbufferDelegateHandle = OnBackbuffer->AddSP(this, &UE::PixelStreaming::FTextureSourceBackBuffer::OnBackBufferReady_RenderThread);
	});
}

UE::PixelStreaming::FTextureSourceBackBuffer::FTextureSourceBackBuffer()
	: UE::PixelStreaming::FTextureSourceBackBuffer(1.0) {}

UE::PixelStreaming::FTextureSourceBackBuffer::~FTextureSourceBackBuffer()
{
	if (OnBackbuffer)
	{
		OnBackbuffer->Remove(BackbufferDelegateHandle);
	}

	SetEnabled(false);
}

void UE::PixelStreaming::FTextureSourceBackBuffer::SetEnabled(bool bInEnabled)
{
	*bEnabled = bInEnabled;

	// This source has been disabled, so set `bInitialized` to false so `OnBackBufferReady_RenderThread`
	// will make new textures next time it is called.
	if (bInitialized && bInEnabled == false)
	{
		bInitialized = false;
	}
}

void UE::PixelStreaming::FTextureSourceBackBuffer::OnBackBufferReady_RenderThread(SWindow& SlateWindow, const FTexture2DRHIRef& FrameBuffer)
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

		UE::PixelStreaming::CopyTexture(FrameBuffer, WriteBuffer.Texture, WriteBuffer.Fence);

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

FTexture2DRHIRef UE::PixelStreaming::FTextureSourceBackBuffer::GetTexture()
{
	if (bIsTempDirty)
	{
		FScopeLock Lock(&CriticalSection);
		ReadBuffer.Swap(TempBuffer);
		bIsTempDirty = false;
	}
	return ReadBuffer;
}

void UE::PixelStreaming::FTextureSourceBackBuffer::Initialize(int Width, int Height)
{
	SourceWidth = Width;
	SourceHeight = Height;

	for (auto& Buffer : WriteBuffers)
	{
		Buffer.Texture = UE::PixelStreaming::CreateTexture(SourceWidth, SourceHeight);
		Buffer.Fence = GDynamicRHI->RHICreateGPUFence(TEXT("VideoCapturerCopyFence"));
		Buffer.bAvailable = true;
	}
	bWriteParity = true;

	TempBuffer = UE::PixelStreaming::CreateTexture(SourceWidth, SourceHeight);
	ReadBuffer = UE::PixelStreaming::CreateTexture(SourceWidth, SourceHeight);
	bIsTempDirty = false;

	bInitialized = true;
}
