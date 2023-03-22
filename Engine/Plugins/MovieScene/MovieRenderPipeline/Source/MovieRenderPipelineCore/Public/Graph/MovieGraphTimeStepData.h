// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MovieGraphTimeStepData.generated.h"

/** 
* This data structure needs to be filled out each frame by the UMovieGraphTimeStepBase,
* which will eventually be passed to the renderer. It controls per-sample behavior such
* as the delta time, if this is the first/last sample for an output frame, etc.
*/
USTRUCT(BlueprintType)
struct MOVIERENDERPIPELINECORE_API FMovieGraphTimeStepData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Movie Graph")
	int32 OutputFrameNumber;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Movie Graph")
	float FrameDeltaTime;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Movie Graph")
	float WorldTimeDilation;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Movie Graph")
	float WorldSeconds;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Movie Graph")
	float MotionBlurFraction;

	/** 
	* Should be set to true for the first sample of each output frame. Used to determine
	* if various systems need to reset or gather data for a new frame. Can be true at
	* the same time as bIsLastTemporalSampleForFrame (ie: 1TS)
	*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Movie Graph")
	bool bIsFirstTemporalSampleForFrame;

	/**
	* Should be set to true for the last sample of each output frame. Can be true at
	* the same time as bIsFirstTemporalSampleForFrame (ie: 1TS)
	*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Movie Graph")
	bool bIsLastTemporalSampleForFrame;
};
