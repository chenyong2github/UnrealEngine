// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineOutputBuilder.h"


// When the Movie Pipeline Render goes to produce a frame, it will push an expected output frame into
// this list. This gets handed all of the component parts of that frame in an arbitrary order/threads
// so we just wait and gather everything that comes in until a frame is complete and then we can kick
// it off to the output step. Because there's potentially a long delay between asking a frame to be
// produced, and it actually being produced, this is responsible for notifying the output containers
// about shot changes, etc. - This has to be pushed back to the Game Thread though because those want to
// live in UObject land for settings.

FMoviePipelineMergerOutputFrame& FMoviePipelineOutputMerger::QueueOutputFrame_GameThread(const FMoviePipelineFrameOutputState& CachedOutputState)
{
	// Lock the ActiveData while we get a new output frame.
	FScopeLock ScopeLock(&ActiveDataMutex);

	FMoviePipelineMergerOutputFrame& NewFrame = ActiveData.FindOrAdd(CachedOutputState);
	NewFrame.FrameOutput = CachedOutputState;

	return NewFrame;
}

void FMoviePipelineOutputMerger::OnDataAvailable_AnyThread(TUniquePtr<FImagePixelData> InData)
{
	// Lock the ActiveData when we're updating what data has been gathered.
	FScopeLock ScopeLock(&ActiveDataMutex);

	
}

// 
// void OnDataAvailable(ImageData, Frame)
// {
// 	QueuedData[FrameNumber][Pass].Ready();
// 
// 	if (QueuedData[FrameNumber].AllReady())
// 	{
// 
// 	}
// }