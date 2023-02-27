// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Graph/MovieGraphDataTypes.h"
#include "MovieGraphDefaultRenderer.generated.h"

/**
* This class 
*/
UCLASS(BlueprintType)
class MOVIERENDERPIPELINECORE_API UMovieGraphDefaultRenderer : public UMovieGraphRendererBase
{
	GENERATED_BODY()
public:
	virtual void Render(const FMovieGraphTimeStepData& InTimeData) override;
	virtual void SetupRenderingPipelineForShot(UMoviePipelineExecutorShot* InShot) override;
	virtual void TeardownRenderingPipelineForShot(UMoviePipelineExecutorShot* InShot) override;
};