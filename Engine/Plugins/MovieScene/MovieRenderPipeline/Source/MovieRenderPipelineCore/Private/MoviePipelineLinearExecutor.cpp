// Copyright Epic Games, Inc. All Rights Reserved.
#include "MoviePipelineLinearExecutor.h"
#include "MovieRenderPipelineCoreModule.h"
#include "MoviePipelineQueue.h"

#define LOCTEXT_NAMESPACE "MoviePipelineLinearExecutorBase"

void UMoviePipelineLinearExecutorBase::ExecuteImpl(UMoviePipelineQueue* InPipelineQueue)
{
	check(InPipelineQueue);

	if (InPipelineQueue->GetJobs().Num() == 0)
	{
		UE_LOG(LogMovieRenderPipeline, Warning, TEXT("Executor asked to execute on empty list of pipelines."));
		OnExecutorErroredImpl(nullptr, true, LOCTEXT("EmptyPipelineError", "Executor asked to execute empty list of jobs. This was probably not intended!"));
		OnExecutorFinishedImpl();
		return;
	}

	// We'll process them in linear fashion and wait until each one is canceled or finishes on its own
	// before moving onto the next one. This may be parallelizable in the future (either multiple PIE
	// sessions, or multiple external processes) but ideally one render would maximize resource usage anyways...
	Queue = InPipelineQueue;
	InitializationTime = FDateTime::UtcNow();

	UE_LOG(LogMovieRenderPipeline, Log, TEXT("MoviePipelineLinearExecutorBase starting %d jobs."), InPipelineQueue->GetJobs().Num());

	StartPipelineByIndex(0);
}

void UMoviePipelineLinearExecutorBase::StartPipelineByIndex(int32 InPipelineIndex)
{
	check(InPipelineIndex >= 0 && InPipelineIndex < Queue->GetJobs().Num());
	
	CurrentPipelineIndex = InPipelineIndex;

	if (!Queue->GetJobs()[CurrentPipelineIndex]->GetConfiguration())
	{
		UE_LOG(LogMovieRenderPipeline, Warning, TEXT("Found null config in list of configs to render. Aborting the pipeline processing!"));
		OnExecutorErroredImpl(nullptr, true, LOCTEXT("NullPipelineError", "Found null config in list of configs to render with. Does your config have the wrong outer?"));
		OnExecutorFinishedImpl();
		return;
	}
	
	UE_LOG(LogMovieRenderPipeline, Log, TEXT("MoviePipelineLinearExecutorBase starting jobs [%d/%d]"), CurrentPipelineIndex, Queue->GetJobs().Num());
	Start(Queue->GetJobs()[CurrentPipelineIndex]);
}

void UMoviePipelineLinearExecutorBase::OnIndividualPipelineFinished(UMoviePipeline* /* FinishedPipeline */)
{
	if (CurrentPipelineIndex == Queue->GetJobs().Num() - 1)
	{
		OnExecutorFinishedImpl();
	}
	else
	{
		// Onto the next one!
		StartPipelineByIndex(CurrentPipelineIndex + 1);
	}
}

void UMoviePipelineLinearExecutorBase::OnPipelineErrored(UMoviePipeline* InPipeline, bool bIsFatal, FText ErrorText)
{
	OnExecutorErroredImpl(InPipeline, bIsFatal, ErrorText);
}

void UMoviePipelineLinearExecutorBase::OnExecutorFinishedImpl()
{
	UE_LOG(LogMovieRenderPipeline, Log, TEXT("MoviePipelineLinearExecutorBase finished %d jobs in %s."), Queue->GetJobs().Num(), *(FDateTime::UtcNow() - InitializationTime).ToString());

	Super::OnExecutorFinishedImpl();
}

#undef LOCTEXT_NAMESPACE // "MoviePipelineLinearExecutorBase"
