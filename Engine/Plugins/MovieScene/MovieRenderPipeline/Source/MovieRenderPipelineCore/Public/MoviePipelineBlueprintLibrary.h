// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "MovieRenderPipelineDataTypes.h"

#include "MoviePipelineBlueprintLibrary.generated.h"

// Forward Declare
class UMoviePipeline;

UCLASS()
class MOVIERENDERPIPELINECORE_API UMoviePipelineBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** 
	* Get the total number of Camera Cuts in a Movie Pipeline, and an index of which one is being processed.
	* Note: Index doesn't index into any arrays (since internally we store CameraCuts inside of Shots)
	*/
	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	static void GetCameraCutCounts(const UMoviePipeline* InPipeline, int32& OutTotalCuts, int32& OutCurrentCutIndex);
	
	/** 
	* Returns the number of expected frames to be produced by Initial Range + Handle Frames given a Frame Rate.
	* This will be inaccurate when PlayRate tracks are involved.
	*/
	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	static int32 GetOutputFrameCountEstimate(const FMoviePipelineShotCutCache& InCameraCut);
	
	/**
	* Returns the expected number of frames different frames that will be submitted for rendering. This is
	* the number of output frames * temporal samples. This will be inaccurate when PlayRate tracks are involved.
	*/
	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	static int32 GetTemporalFrameCountEstimate(const FMoviePipelineShotCutCache& InCameraCut);
	
	/** 
	* Returns misc. utility frame counts (Warm Up + MotionBlur Fix) since these are outside the ranges.
	*/
	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	static int32 GetUtilityFrameCountEstimate(const FMoviePipelineShotCutCache& InCameraCut);
	
	/**
	* Returns the number of samples submitted to the GPU, optionally counting samples for the various parts.
	* This will be inaccurate when PlayRate tracks are involved.
	*/
	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	static int32 GetSampleCountEstimate(const FMoviePipelineShotCutCache& InCameraCut, const bool bIncludeWarmup = true, const bool bIncludeMotionBlur = true);
};