// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineBlueprintLibrary.h"
#include "MoviePipeline.h"
#include "MovieRenderPipelineDataTypes.h"

void UMoviePipelineBlueprintLibrary::GetCameraCutCounts(const UMoviePipeline* InPipeline, int32& OutTotalCuts, int32& OutCurrentCutIndex)
{
	OutTotalCuts = 0;
	OutCurrentCutIndex = 0;
	
	if(!InPipeline)
	{
		return;
	}

	const TArray<FMoviePipelineShotCache>&  ShotList = InPipeline->GetShotList();
	for (int32 ShotIdx = 0; ShotIdx < ShotList.Num(); ShotIdx++)
	{
		OutTotalCuts += ShotList[ShotIdx].CameraCuts.Num();

		if (ShotIdx <= InPipeline->GetCurrentShotIndex())
		{
			// We don't add an additional one here because we're currently processing that
			// index.
			OutCurrentCutIndex += ShotList[ShotIdx].CurrentCameraCutIndex;
		}
	}
}

int32 UMoviePipelineBlueprintLibrary::GetOutputFrameCountEstimate(const FMoviePipelineShotCutCache& InCameraCut)
{
	return InCameraCut.GetOutputFrameCountEstimate().Value;
}

int32 UMoviePipelineBlueprintLibrary::GetTemporalFrameCountEstimate(const FMoviePipelineShotCutCache& InCameraCut)
{
	return InCameraCut.GetTemporalFrameCountEstimate().Value;
}

int32 UMoviePipelineBlueprintLibrary::GetUtilityFrameCountEstimate(const FMoviePipelineShotCutCache& InCameraCut)
{
	return InCameraCut.GetUtilityFrameCountEstimate().Value;
}

int32 UMoviePipelineBlueprintLibrary::GetSampleCountEstimate(const FMoviePipelineShotCutCache& InCameraCut, const bool bIncludeWarmup, const bool bIncludeMotionBlur)
{
	return InCameraCut.GetSampleCountEstimate(bIncludeWarmup, bIncludeMotionBlur).Value;
}
