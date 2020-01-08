// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineOutputBuilder.h"
#include "MoviePipeline.h"
#include "Async/Async.h"


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

	// Ensure this frame hasn't already been entered somehow.
	check(!ActiveData.Find(CachedOutputState));

	FMoviePipelineMergerOutputFrame& NewFrame = ActiveData.Add(CachedOutputState);
	NewFrame.FrameOutputState = CachedOutputState;

	return NewFrame;
}

void FMoviePipelineOutputMerger::OnSingleSampleDataAvailable_AnyThread(TUniquePtr<FImagePixelData>&& InData, const TSharedRef<FImagePixelDataPayload, ESPMode::ThreadSafe> InFrameData)
{
	// This is to support outputting individual samples (skipping accumulation) for debug reasons,
	// or because you want to post-process them yourself. We just forward this directly on for output to disk.

	TWeakObjectPtr<UMoviePipeline> LocalWeakPipeline = WeakMoviePipeline;

	AsyncTask(ENamedThreads::GameThread, [LocalData = MoveTemp(InData), InFrameData, LocalWeakPipeline]() mutable
	{
		if (ensureAlwaysMsgf(LocalWeakPipeline.IsValid(), TEXT("A memory lifespan issue has left an output builder alive without an owning Movie Pipeline.")))
		{
			LocalWeakPipeline->OnSampleRendered(MoveTemp(LocalData), InFrameData);
		}
	}
	);
}

void FMoviePipelineOutputMerger::OnCompleteRenderPassDataAvailable_AnyThread(TUniquePtr<FImagePixelData>&& InData, const TSharedRef<FImagePixelDataPayload, ESPMode::ThreadSafe> InFrameData)
{
	// Lock the ActiveData when we're updating what data has been gathered.
	FScopeLock ScopeLock(&ActiveDataMutex);

	// See if we can find the frame this data is for. This should always be valid, if it's not
	// valid it means they either forgot to declare they were going to produce it, or this is
	// coming in after the system already thinks it's finished that frame.
	FMoviePipelineMergerOutputFrame* OutputFrame = nullptr;
	
	// Instead of just finding the result in the TMap with the equality operator, we find it by hand so that we can
	// ignore certain parts of equality (such as Temporal Sample, as the last sample has a temporal index different
	// than the first sample!)
	for (TPair<FMoviePipelineFrameOutputState, FMoviePipelineMergerOutputFrame>& KVP : ActiveData)
	{
		if (KVP.Key.OutputFrameNumber == InFrameData->OutputState.OutputFrameNumber)
	 	{
	 		OutputFrame = &KVP.Value;
	 		break;
	 	}
	}
	if (!ensureAlwaysMsgf(OutputFrame, TEXT("Recieved data for unknown frame. Frame was either already processed or not queued yet!")))
	{
		return;
	}

	// Ensure this pass is expected as well...
	if (!ensureAlwaysMsgf(OutputFrame->ExpectedRenderPasses.Contains(InFrameData->PassIdentifier), TEXT("Recieved data for unexpected render pass: %s"), *InFrameData->PassIdentifier.Name))
	{
		return;
	}

	// If this data was expected and this frame is still in progress, pass the data to the frame.
	OutputFrame->ImageOutputData.FindOrAdd(InFrameData->PassIdentifier) = MoveTemp(InData);

	// Check to see if this was the last piece of data needed for this frame.
	int32 TotalPasses = OutputFrame->ExpectedRenderPasses.Num();
	int32 SucceededPasses = OutputFrame->ImageOutputData.Num();

	if (SucceededPasses == TotalPasses)
	{
		// Transfer ownership from the map to here;
		FMoviePipelineMergerOutputFrame FinalFrame;
		ActiveData.RemoveAndCopyValue(InFrameData->OutputState, FinalFrame);

		// Notify the Movie Pipeline that this frame has been completed and can be forwarded to the output containers.
		TWeakObjectPtr<UMoviePipeline> LocalWeakPipeline = WeakMoviePipeline;

		AsyncTask(ENamedThreads::GameThread, [LocalFinalFrame = MoveTemp(FinalFrame), InFrameData, LocalWeakPipeline]() mutable
		{
			if (ensureAlwaysMsgf(LocalWeakPipeline.IsValid(), TEXT("A memory lifespan issue has left an output builder alive without an owning Movie Pipeline.")))
			{
				LocalWeakPipeline->OnFrameCompletelyRendered(MoveTemp(LocalFinalFrame), InFrameData);
			}
		}
		);
	}
}