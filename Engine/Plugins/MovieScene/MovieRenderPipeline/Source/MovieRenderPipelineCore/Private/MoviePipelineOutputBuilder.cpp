// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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

void FMoviePipelineOutputMerger::OnSingleSampleDataAvailable_AnyThread(TUniquePtr<FImagePixelData>&& InData)
{
	// This is to support outputting individual samples (skipping accumulation) for debug reasons,
	// or because you want to post-process them yourself. We just forward this directly on for output to disk.

	TWeakObjectPtr<UMoviePipeline> LocalWeakPipeline = WeakMoviePipeline;

	AsyncTask(ENamedThreads::GameThread, [LocalData = MoveTemp(InData), LocalWeakPipeline]() mutable
	{
		if (ensureAlwaysMsgf(LocalWeakPipeline.IsValid(), TEXT("A memory lifespan issue has left an output builder alive without an owning Movie Pipeline.")))
		{
			LocalWeakPipeline->OnSampleRendered(MoveTemp(LocalData));
		}
	}
	);
}

void FMoviePipelineOutputMerger::OnCompleteRenderPassDataAvailable_AnyThread(TUniquePtr<FImagePixelData>&& InData)
{
	// Lock the ActiveData when we're updating what data has been gathered.
	FScopeLock ScopeLock(&ActiveDataMutex);

	FImagePixelDataPayload* Payload = InData->GetPayload<FImagePixelDataPayload>();
	check(Payload);

	// See if we can find the frame this data is for. This should always be valid, if it's not
	// valid it means they either forgot to declare they were going to produce it, or this is
	// coming in after the system already thinks it's finished that frame.
	FMoviePipelineMergerOutputFrame* OutputFrame = ActiveData.Find(Payload->OutputState);
	if (!ensureAlwaysMsgf(OutputFrame, TEXT("Recieved data for unknown frame. Frame was either already processed or not queued yet!")))
	{
		return;
	}

	// Ensure this pass is expected as well...
	if (!ensureAlwaysMsgf(OutputFrame->ExpectedRenderPasses.Contains(Payload->PassIdentifier), TEXT("Recieved data for unexpected render pass: %s"), *Payload->PassIdentifier.Name))
	{
		return;
	}

	// If this data was expected and this frame is still in progress, pass the data to the frame.
	OutputFrame->ImageOutputData.FindOrAdd(Payload->PassIdentifier) = MoveTemp(InData);

	// Check to see if this was the last piece of data needed for this frame.
	int32 TotalPasses = OutputFrame->ExpectedRenderPasses.Num();
	int32 SucceededPasses = OutputFrame->ImageOutputData.Num();

	if (SucceededPasses == TotalPasses)
	{
		// Transfer ownership from the map to here;
		FMoviePipelineMergerOutputFrame FinalFrame;
		ActiveData.RemoveAndCopyValue(Payload->OutputState, FinalFrame);

		// Notify the Movie Pipeline that this frame has been completed and can be forwarded to the output containers.
		TWeakObjectPtr<UMoviePipeline> LocalWeakPipeline = WeakMoviePipeline;

		AsyncTask(ENamedThreads::GameThread, [LocalFinalFrame = MoveTemp(FinalFrame), LocalWeakPipeline]() mutable
		{
			if (ensureAlwaysMsgf(LocalWeakPipeline.IsValid(), TEXT("A memory lifespan issue has left an output builder alive without an owning Movie Pipeline.")))
			{
				LocalWeakPipeline->OnFrameCompletelyRendered(MoveTemp(LocalFinalFrame));
			}
		}
		);
	}
}