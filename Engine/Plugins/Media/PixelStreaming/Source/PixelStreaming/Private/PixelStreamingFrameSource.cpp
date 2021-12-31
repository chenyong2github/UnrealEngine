// Copyright Epic Games, Inc. All Rights Reserved.
#include "PixelStreamingFrameSource.h"

#include "LatencyTester.h"
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

	// Latency test pre capture
	if (FLatencyTester::IsTestRunning() && FLatencyTester::GetTestStage() == FLatencyTester::ELatencyTestStage::PRE_CAPTURE)
	{
		FLatencyTester::RecordPreCaptureTime();
	}

	// pass the frame to the layers
	for (auto& LayerSource : LayerSources)
	{
		LayerSource->OnFrameReady(FrameBuffer);
	}

	// Latency test post capture
	if (FLatencyTester::IsTestRunning() && FLatencyTester::GetTestStage() == FLatencyTester::ELatencyTestStage::POST_CAPTURE)
	{
		FLatencyTester::RecordPostCaptureTime(FrameId);
	}

	UE_LOG(PixelStreamer, VeryVerbose, TEXT("(%d) captured video %lld"), RtcTimeMs(), TimestampUs);

	// If stats are enabled, records the stats during capture now.
	FPixelStreamingStats& Stats = FPixelStreamingStats::Get();
	if (Stats.GetStatsEnabled())
	{
		int64 TimestampNowUs = rtc::TimeMicros();
		int64 CaptureLatencyUs = TimestampNowUs - TimestampUs;
		double CaptureLatencyMs = (double)CaptureLatencyUs / 1000.0;
		Stats.SetCaptureLatency(CaptureLatencyMs);
		Stats.OnCaptureFinished();
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
:FrameScale(InFrameScale)
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

		RHICmdList.EnqueueLambda([&](FRHICommandListImmediate& RHICmdList){
			WriteBuffer.PreWaitingOnCopy = FPlatformTime::Cycles64();
		});

		CopyTexture(FrameBuffer, WriteBuffer.Texture, WriteBuffer.Fence);

		AsyncTask(ENamedThreads::AnyHiPriThreadHiPriTask, [&]()
		{
			while(!WriteBuffer.Fence->Poll())
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

			uint64 PostWaitingOnCopy = FPlatformTime::Cycles64();

			FPixelStreamingStats& Stats = FPixelStreamingStats::Get();
			if(Stats.GetStatsEnabled())
			{
				double CaptureLatencyMs = FPlatformTime::ToMilliseconds64(PostWaitingOnCopy - WriteBuffer.PreWaitingOnCopy);
				Stats.SetCaptureLatency(CaptureLatencyMs);
				Stats.OnCaptureFinished();
			}
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
