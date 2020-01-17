// Copyright Epic Games, Inc. All Rights Reserved.
#include "MoviePipelineInProcessExecutor.h"
#include "MoviePipeline.h"
#include "Engine/World.h"
#include "MoviePipelineQueue.h"

#define LOCTEXT_NAMESPACE "MoviePipelineInProcessExecutor"
void UMoviePipelineInProcessExecutor::Start(const UMoviePipelineExecutorJob* InJob)
{
	FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UMoviePipelineInProcessExecutor::OnMapLoadFinished);
}

void UMoviePipelineInProcessExecutor::OnMapLoadFinished(UWorld* NewWorld)
{
	// NewWorld can be null if a world is being destroyed.
	if (!NewWorld)
	{
		return;
	}

	// Stop listening for map load until we're done and know we want to start the next config.
	FCoreUObjectDelegates::PostLoadMapWithWorld.RemoveAll(this);
	
	UMoviePipelineExecutorJob* CurrentJob = Queue->GetJobs()[CurrentPipelineIndex];

	ActiveMoviePipeline = NewObject<UMoviePipeline>(NewWorld, TargetPipelineClass);
	ActiveMoviePipeline->Initialize(CurrentJob);

	// Listen for when the pipeline thinks it has finished.
	ActiveMoviePipeline->OnMoviePipelineFinished().AddUObject(this, &UMoviePipelineInProcessExecutor::OnMoviePipelineFinished);
}

void UMoviePipelineInProcessExecutor::OnMoviePipelineFinished(UMoviePipeline* InMoviePipeline)
{
	UMoviePipeline* MoviePipeline = ActiveMoviePipeline;

	if (ActiveMoviePipeline)
	{
		// Unsubscribe in the event that it gets called twice we don't have issues.
		ActiveMoviePipeline->OnMoviePipelineFinished().RemoveAll(this);
	}

	// Null these out now since OnIndividualPipelineFinished might invoke something that causes a GC
	// and we want them to go away with the GC.
	ActiveMoviePipeline = nullptr;

	// Now that another frame has passed and we should be OK to start another PIE session, notify our owner.
	OnIndividualPipelineFinished(MoviePipeline);
}
#undef LOCTEXT_NAMESPACE // "MoviePipelineInProcessExecutor"
