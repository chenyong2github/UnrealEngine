// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "MovieRenderPipelineDataTypes.h"

// Forward Declares
struct FImagePixelData;

struct FMoviePipelineMergerOutputFrame
{
	FMoviePipelineFrameOutputState FrameOutput;
	TArray<FMoviePipelinePassIdentifier> ExpectedRenderPasses;
};

class FMoviePipelineOutputMerger
{
public:
	FMoviePipelineOutputMerger()
	{
	}

public:
	FMoviePipelineMergerOutputFrame& QueueOutputFrame_GameThread(const FMoviePipelineFrameOutputState& CachedOutputState);
	void OnDataAvailable_AnyThread(TUniquePtr<FImagePixelData> InData);

private:
	TMap<FMoviePipelineFrameOutputState, FMoviePipelineMergerOutputFrame> ActiveData;

	/** Mutex that protects adding/updating/removing from ActiveData */
	FCriticalSection ActiveDataMutex;
};


