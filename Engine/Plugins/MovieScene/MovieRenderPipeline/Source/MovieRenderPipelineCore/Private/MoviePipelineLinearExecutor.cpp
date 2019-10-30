// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "MoviePipelineLinearExecutor.h"
#include "MovieRenderPipelineCoreModule.h"

#define LOCTEXT_NAMESPACE "MoviePipelineLinearExecutorBase"

void UMoviePipelineLinearExecutorBase::ExecuteImpl(TArray<UMovieRenderPipelineConfig*>& InPipelines)
{
	if (InPipelines.Num() == 0)
	{
		UE_LOG(LogMovieRenderPipeline, Warning, TEXT("Executor asked to execute on empty list of pipelines."));
		OnExecutorFinishedImpl(false);
		return;
	}

	// We'll process them in linear fashion and wait until each one is canceled or finishes on its own
	// before moving onto the next one. This may be parallelizable in the future (either multiple PIE
	// sessions, or multiple external processes) but ideally one render would maximize resource usage anyways...
	Pipelines = InPipelines;

	InitializationTime = FDateTime::UtcNow();

	StartPipelineByIndex(0);
}

void UMoviePipelineLinearExecutorBase::StartPipelineByIndex(int32 InPipelineIndex)
{
	check(InPipelineIndex >= 0 && InPipelineIndex < Pipelines.Num());
	
	CurrentPipelineIndex = InPipelineIndex;

	if (!Pipelines[CurrentPipelineIndex])
	{
		UE_LOG(LogMovieRenderPipeline, Warning, TEXT("Found null config in list of configs to render. Aborting the pipeline processing!"));
		OnExecutorFinishedImpl(false);
		return;
	}
	
	Start(Pipelines[CurrentPipelineIndex], CurrentPipelineIndex, Pipelines.Num());
}

void UMoviePipelineLinearExecutorBase::OnIndividualPipelineFinished(UMoviePipeline* /* FinishedPipeline */)
{
	if (CurrentPipelineIndex == Pipelines.Num() - 1)
	{
		OnExecutorFinishedImpl(true);
	}
	else
	{
		// Onto the next one!
		StartPipelineByIndex(CurrentPipelineIndex + 1);
	}
}

void UMoviePipelineLinearExecutorBase::OnExecutorFinishedImpl(const bool bInSuccess)
{
	UE_LOG(LogMovieRenderPipeline, Log, TEXT("MoviePipelineLinearExecutorBase produced %d Pipelines in %s."), Pipelines.Num(), *(FDateTime::UtcNow() - InitializationTime).ToString());

	Super::OnExecutorFinishedImpl(bInSuccess);
}

#undef LOCTEXT_NAMESPACE // "MoviePipelineLinearExecutorBase"
