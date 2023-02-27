// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/MovieGraphDefaultRenderer.h"
#include "Graph/MovieGraphPipeline.h"

void UMovieGraphDefaultRenderer::Render(const FMovieGraphTimeStepData& InTimeStepData)
{
	UE_LOG(LogTemp, Warning, TEXT("MovieGraphRenderer: Render"));
}

void UMovieGraphDefaultRenderer::SetupRenderingPipelineForShot(UMoviePipelineExecutorShot* InShot)
{
	UE_LOG(LogTemp, Warning, TEXT("MovieGraphRenderer: SetupForShot"));

}

void UMovieGraphDefaultRenderer::TeardownRenderingPipelineForShot(UMoviePipelineExecutorShot* InShot)
{
	UE_LOG(LogTemp, Warning, TEXT("MovieGraphRenderer: TeardownForShot"));
}