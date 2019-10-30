// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MovieRenderPipelineDataTypes.h"
#include "MovieRenderPipelineCoreModule.h"
#include "Sections/MovieSceneCinematicShotSection.h"
#include "MoviePipelineAccumulationSetting.h"

FString FMoviePipelineShotCache::GetDisplayName() const
{
	if (CinematicShotSection.Get())
	{
		return CinematicShotSection->GetShotDisplayName();
	}

	return TEXT("Unnamed");
}

FFrameNumber FMoviePipelineShotCutCache::GetOutputFrameCountEstimate() const
{
	// TotalRange is stored in Tick Resolution, so we convert 1 frame of Frame Rate to the number of ticks.
	FFrameNumber OneFrameInTicks = FFrameRate::TransformTime(FFrameTime(FFrameNumber(1)), CachedFrameRate, CachedTickResolution).FloorToFrame();

	// Find out how many ticks long our total output range is.
	FFrameNumber TotalOutputRangeTicks = TotalOutputRange.Size<FFrameNumber>();
	int32 NumFrames = FMath::CeilToInt(TotalOutputRangeTicks.Value / (double)OneFrameInTicks.Value);

	return FFrameNumber(NumFrames);
}

FFrameNumber FMoviePipelineShotCutCache::GetTemporalFrameCountEstimate() const
{
	FFrameNumber OutputEstimate = GetOutputFrameCountEstimate();
	FFrameNumber NumRenderedSamples = OutputEstimate * NumTemporalSamples;

	return NumRenderedSamples;
}

FFrameNumber FMoviePipelineShotCutCache::GetUtilityFrameCountEstimate() const
{
	return FFrameNumber(NumWarmUpFrames + (bAccurateFirstFrameHistory ? 1 : 0));
}

FFrameNumber FMoviePipelineShotCutCache::GetSampleCountEstimate(const bool bIncludeWarmup, const bool bIncludeMotionBlur) const
{
	int32 TotalSamples = 0;
	
	// Warm Up Frames currently don't submit anything to the GPU.
	if (bIncludeWarmup)
	{
		TotalSamples += 0;
	}

	// Motion blur only adds one frame
	if (bIncludeMotionBlur)
	{
		TotalSamples += 1;
	}

	// Only the main frames are rendered with Temporal sub-sampling and Spatial sub-sampling.
	TotalSamples += GetOutputFrameCountEstimate().Value * NumTemporalSamples * NumSpatialSamples;

	return FFrameNumber(TotalSamples);
}

void FMoviePipelineShotCutCache::SetNextState(const EMovieRenderShotState InCurrentState)
{
	switch (InCurrentState)
	{
		// This may get called multiple times so we just do nothing until it's appropriate to move on from WarmingUp.
	case EMovieRenderShotState::WarmingUp:
		// Warming Up can jump directly to either Rendering or to MotionBlur depending on if fixes are applied.
		if (NumWarmUpFramesRemaining == 0)
		{
			if (bAccurateFirstFrameHistory)
			{
				UE_LOG(LogMovieRenderPipeline, Verbose, TEXT("[%d] Shot WarmUp finished. Setting state to MotionBlur."), GFrameCounter);
				State = EMovieRenderShotState::MotionBlur;
			}
			else
			{
				UE_LOG(LogMovieRenderPipeline, Verbose, TEXT("[%d] Shot WarmUp finished. Setting state to Rendering due to no MotionBlur pre-frames."), GFrameCounter);
				State = EMovieRenderShotState::Rendering;
			}
		}
		break;
		// This should only be called once with the Uninitalized state.
	case EMovieRenderShotState::Uninitialized:
		// Uninitialized can either jump to WarmUp, MotionBlur, or straight to Rendering if no fixes are applied.
		if (NumWarmUpFramesRemaining > 0)
		{
			UE_LOG(LogMovieRenderPipeline, Log, TEXT("[%d] Initialization set state to WarmingUp due to having %d warm up frames."), GFrameCounter, NumWarmUpFramesRemaining);
			State = EMovieRenderShotState::WarmingUp;
		}
		// If they didn't want any warm-up frames we'll still check to see if they want to fix motion blur on the first frame.
		else if (bAccurateFirstFrameHistory)
		{
			UE_LOG(LogMovieRenderPipeline, Log, TEXT("[%d] Initialization set state to MotionBlur due to having no warm up frames."), GFrameCounter);
			State = EMovieRenderShotState::MotionBlur;
		}
		else
		{
			UE_LOG(LogMovieRenderPipeline, Verbose, TEXT("[%d] Initialization set state to Rendering due to no MotionBlur pre-frames."), GFrameCounter);
			State = EMovieRenderShotState::Rendering;
		}
		break;
	}

}