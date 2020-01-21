// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "MovieRenderPipelineDataTypes.h"

// Forward Declares
struct FImagePixelData;
class UMoviePipeline;



class MOVIERENDERPIPELINECORE_API FMoviePipelineOutputMerger
{
public:
	FMoviePipelineOutputMerger(UMoviePipeline* InOwningMoviePipeline)
		: WeakMoviePipeline(MakeWeakObjectPtr(InOwningMoviePipeline))
	{
	}

public:
	FMoviePipelineMergerOutputFrame& QueueOutputFrame_GameThread(const FMoviePipelineFrameOutputState& CachedOutputState);
	void OnCompleteRenderPassDataAvailable_AnyThread(TUniquePtr<FImagePixelData>&& InData, const TSharedRef<FImagePixelDataPayload, ESPMode::ThreadSafe> InFrameData);
	void OnSingleSampleDataAvailable_AnyThread(TUniquePtr<FImagePixelData>&& InData, const TSharedRef<FImagePixelDataPayload, ESPMode::ThreadSafe> InFrameData);

private:
	/** The Movie Pipeline that owns us. */
	TWeakObjectPtr<UMoviePipeline> WeakMoviePipeline;

	/** Data that is expected but not fully available yet. */
	TMap<FMoviePipelineFrameOutputState, FMoviePipelineMergerOutputFrame> ActiveData;

	/** Mutex that protects adding/updating/removing from ActiveData */
	FCriticalSection ActiveDataMutex;
};


