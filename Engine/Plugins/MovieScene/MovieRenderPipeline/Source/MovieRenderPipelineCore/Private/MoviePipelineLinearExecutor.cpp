// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "MoviePipelineLinearExecutor.h"
#include "MovieRenderPipelineCoreModule.h"

#define LOCTEXT_NAMESPACE "MoviePipelineLinearExecutorBase"

void UMoviePipelineLinearExecutorBase::ExecuteImpl(const TArray<FMoviePipelineExecutorJob>& InPipelines)
{
	if (InPipelines.Num() == 0)
	{
		UE_LOG(LogMovieRenderPipeline, Warning, TEXT("Executor asked to execute on empty list of pipelines."));
		OnExecutorErroredImpl(nullptr, true, LOCTEXT("EmptyPipelineError", "Executor asked to execute empty list of jobs. This was probably not intended!"));
		OnExecutorFinishedImpl();
		return;
	}

	// We'll process them in linear fashion and wait until each one is canceled or finishes on its own
	// before moving onto the next one. This may be parallelizable in the future (either multiple PIE
	// sessions, or multiple external processes) but ideally one render would maximize resource usage anyways...
	ExecutorJobs = InPipelines;
	InitializationTime = FDateTime::UtcNow();

	UE_LOG(LogMovieRenderPipeline, Log, TEXT("MoviePipelineLinearExecutorBase starting %d jobs."), InPipelines.Num());

	StartPipelineByIndex(0);
}

void UMoviePipelineLinearExecutorBase::StartPipelineByIndex(int32 InPipelineIndex)
{
	check(InPipelineIndex >= 0 && InPipelineIndex < ExecutorJobs.Num());
	
	CurrentPipelineIndex = InPipelineIndex;

	if (!ExecutorJobs[CurrentPipelineIndex].Configuration)
	{
		UE_LOG(LogMovieRenderPipeline, Warning, TEXT("Found null config in list of configs to render. Aborting the pipeline processing!"));
		OnExecutorErroredImpl(nullptr, true, LOCTEXT("NullPipelineError", "Found null config in list of configs to render with. Does your config have the wrong outer?"));
		OnExecutorFinishedImpl();
		return;
	}
	
	UE_LOG(LogMovieRenderPipeline, Log, TEXT("MoviePipelineLinearExecutorBase starting jobs [%d/%d]"), CurrentPipelineIndex, ExecutorJobs.Num());
	Start(ExecutorJobs[CurrentPipelineIndex]);
}

void UMoviePipelineLinearExecutorBase::OnIndividualPipelineFinished(UMoviePipeline* /* FinishedPipeline */)
{
	if (CurrentPipelineIndex == ExecutorJobs.Num() - 1)
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
	UE_LOG(LogMovieRenderPipeline, Log, TEXT("MoviePipelineLinearExecutorBase finished %d jobs in %s."), ExecutorJobs.Num(), *(FDateTime::UtcNow() - InitializationTime).ToString());

	Super::OnExecutorFinishedImpl();
}

#undef LOCTEXT_NAMESPACE // "MoviePipelineLinearExecutorBase"
