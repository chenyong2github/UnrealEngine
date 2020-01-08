// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "MovieRenderPipelineDataTypes.h"

#include "MoviePipelineBlueprintLibrary.generated.h"

// Forward Declare
class UMoviePipeline;
class UMovieSceneSequence;

UCLASS()
class MOVIERENDERPIPELINECORE_API UMoviePipelineBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	* Takes a given Output Directory and FileName which contain {formatStrings} and convert those format strings
	* to their actual values as best as possible. Merges the result into one final filepath string.
	*/
	// UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	// static FString GetCameraCutCounts(const UMoviePipeline* InPipeline, const FMoviePipelineFrameOutputState& OutputState, const FDirectoryPath& InDirectoryFormatString, const FString& InFileNameFormatString);

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
	static int32 GetOutputFrameCountEstimate(const FMoviePipelineCameraCutInfo& InCameraCut);
	
	/**
	* Returns the expected number of frames different frames that will be submitted for rendering. This is
	* the number of output frames * temporal samples. This will be inaccurate when PlayRate tracks are involved.
	*/
	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	static int32 GetTemporalFrameCountEstimate(const FMoviePipelineCameraCutInfo& InCameraCut);
	
	/** 
	* Returns misc. utility frame counts (Warm Up + MotionBlur Fix) since these are outside the ranges.
	*/
	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	static int32 GetUtilityFrameCountEstimate(const FMoviePipelineCameraCutInfo& InCameraCut);
	
	/**
	* Returns the number of samples submitted to the GPU, optionally counting samples for the various parts.
	* This will be inaccurate when PlayRate tracks are involved.
	*/
	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	static int32 GetSampleCountEstimate(const FMoviePipelineCameraCutInfo& InCameraCut, const bool bIncludeWarmup = true, const bool bIncludeMotionBlur = true);
	
	/**
	* Duplicates the specified sequence using a medium depth copy. Standard duplication will only duplicate
	* the top level Sequence (since shots and sub-sequences are other standalone assets) so this function
	* recursively duplicates the given sequence, shot and subsequence and then fixes up the references to
	* point to newly duplicated sequences.
	*
	* This does not duplicate any assets that the sequence points to outside of Shots/Subsequences.
	*
	* @param	Outer		- The Outer of the newly duplicated object. Leave null for TransientPackage();
	* @param	InSequence	- The sequence to recursively duplicate.
	* @return				- The duplicated sequence, or null if no sequence was provided to duplicate.
	*/
	UFUNCTION(BlueprintCallable, Category = "Movie Render Pipeline")
	static UMovieSceneSequence* DuplicateSequence(UObject* Outer, UMovieSceneSequence* InSequence);

	// UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	// 	FMoviePipelineShotInfo GetCurrentShotSnapshot() const;
	// 
	// UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	// 	FMoviePipelineCameraCutInfo GetCurrentCameraCutSnapshot() const;
	// 
	// UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	// 	FMoviePipelineFrameOutputState GetOutputStateSnapshot() const;

	/**
	* Returns an estimate based on the average time taken to render all previous frames.
	* Will be incorrect if PlayRate tracks are in use. Will be inaccurate when different
	* shots take significantly different amounts of time to render.
	*
	* @param OutTimespan: The returned estimated time left. Will be default initialized if there is no estimate.
	* @return True if we can make a reasonable estimate, false otherwise (ie: no rendering has been done to estimate).
	*/
	// UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	// bool GetRemainingTimeEstimate(FTimespan& OutTimespan) const;
};