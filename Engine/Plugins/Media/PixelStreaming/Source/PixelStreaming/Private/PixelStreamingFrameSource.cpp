// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingFrameSource.h"
#include "PixelStreamingStats.h"
#include "Async/Async.h"
#include "Utils.h"

FPixelStreamingFrameSource::FPixelStreamingFrameSource()
{
	// first add to a sorted map then move into the indexed array
	// this is to ensure that the array is sorted in a particular order

	TSortedMap<float, TUniquePtr<FPixelStreamingLayerFrameSource>> SortedLayers;

	// main layer
	SortedLayers.Add(1.0f, MakeUnique<FPixelStreamingLayerFrameSource>(1.0f));

	// simulcast layers
	for (auto& SimulcastLayer : PixelStreamingSettings::SimulcastParameters.Layers)
	{
		const float Scale = 1.0f / SimulcastLayer.Scaling;
		SortedLayers.Add(Scale, MakeUnique<FPixelStreamingLayerFrameSource>(Scale));
	}

	for (auto& Layer : SortedLayers)
	{
		UE_LOG(PixelStreamer, Log, TEXT("Created frame source with scaling factor: %f"), Layer.Value->FrameScale);
		LayerSources.Add(MoveTemp(Layer.Value));
	}
}

FPixelStreamingFrameSource::~FPixelStreamingFrameSource()
{
}

void FPixelStreamingFrameSource::OnFrameReady(const FTexture2DRHIRef& FrameBuffer)
{
	const int32 FrameId = NextFrameId++;
	const int64 TimestampUs = rtc::TimeMicros();

	// pass the frame to the layers
	for (auto& LayerSource : LayerSources)
	{
		LayerSource->OnFrameReady(FrameBuffer);
	}

	bAvailable = true;
}

int FPixelStreamingFrameSource::GetNumLayers() const
{
	return LayerSources.Num();
}

FPixelStreamingLayerFrameSource* FPixelStreamingFrameSource::GetLayerFrameSource(int LayerIndex)
{
	checkf(LayerIndex < LayerSources.Num(), TEXT("Requested source layer out of range."));
	return LayerSources[LayerIndex].Get();
}

int FPixelStreamingFrameSource::GetSourceWidth() const
{
	return LayerSources[LayerSources.Num() - 1]->GetSourceWidth();
}

int FPixelStreamingFrameSource::GetSourceHeight() const
{
	return LayerSources[LayerSources.Num() - 1]->GetSourceHeight();
}

FPixelStreamingLayerFrameSource::FPixelStreamingLayerFrameSource(float InFrameScale)
	: FrameScale(InFrameScale)
{
}

FPixelStreamingLayerFrameSource::~FPixelStreamingLayerFrameSource()
{
}

void FPixelStreamingLayerFrameSource::OnFrameReady(const FTexture2DRHIRef& FrameBuffer)
{
	if (!bInitialized)
	{
		Initialize(FrameBuffer->GetSizeXY().X * FrameScale, FrameBuffer->GetSizeXY().Y * FrameScale);
	}

	auto& WriteBuffer = bWriteParity ? WriteBuffers[0] : WriteBuffers[1];
	bWriteParity = !bWriteParity;

	// for safety we just make sure that the buffer is not currently waiting for a copy
	if (WriteBuffer.bAvailable)
	{
		WriteBuffer.bAvailable = false;

		FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
		WriteBuffer.Fence->Clear();

		RHICmdList.EnqueueLambda([&](FRHICommandListImmediate& RHICmdList) {
			WriteBuffer.PreWaitingOnCopy = FPlatformTime::Cycles64();
		});

		CopyTexture(FrameBuffer, WriteBuffer.Texture, WriteBuffer.Fence);

		AsyncTask(ENamedThreads::AnyHiPriThreadHiPriTask, [&]() {
			while (!WriteBuffer.Fence->Poll())
			{
				// spin
			}

			{
				FScopeLock Lock(&CriticalSection);

				TempBuffer.Swap(WriteBuffer.Texture);
				WriteBuffer.Fence->Clear();
				WriteBuffer.bAvailable = true;

				bIsTempDirty = true;
			}

			// For debugging timing information about the copy operation
			// Turning it on all the time is a bit too much log spam if logging stats
			//uint64 PostWaitingOnCopy = FPlatformTime::Cycles64();
			// FPixelStreamingStats* Stats = FPixelStreamingStats::Get();
			// if(Stats)
			// {
			// 	double CaptureLatencyMs = FPlatformTime::ToMilliseconds64(PostWaitingOnCopy - WriteBuffer.PreWaitingOnCopy);
			// 	Stats->StoreApplicationStat(FStatData(FName(*FString::Printf(TEXT("Layer (x%.2f) Capture time (ms)"), FrameScale)), CaptureLatencyMs, 2, true));
			// }
		});
	}
}

FTexture2DRHIRef FPixelStreamingLayerFrameSource::GetFrame()
{
	if (bIsTempDirty)
	{
		FScopeLock Lock(&CriticalSection);
		ReadBuffer.Swap(TempBuffer);
		bIsTempDirty = false;
	}
	return ReadBuffer;
}

void FPixelStreamingLayerFrameSource::Initialize(int Width, int Height)
{
	SourceWidth = Width;
	SourceHeight = Height;

	for (auto& Buffer : WriteBuffers)
	{
		Buffer.Texture = CreateTexture(SourceWidth, SourceHeight);
		Buffer.Fence = GDynamicRHI->RHICreateGPUFence(TEXT("VideoCapturerCopyFence"));
		Buffer.bAvailable = true;
	}
	bWriteParity = true;

	TempBuffer = CreateTexture(SourceWidth, SourceHeight);
	ReadBuffer = CreateTexture(SourceWidth, SourceHeight);
	bIsTempDirty = false;

	bInitialized = true;
}
