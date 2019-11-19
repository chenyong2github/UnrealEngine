// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineBlueprintLibrary.h"
#include "MoviePipeline.h"
#include "MovieRenderPipelineDataTypes.h"
#include "MovieSceneSequence.h"
#include "Tracks/MovieSceneCinematicShotTrack.h"
#include "Sections/MovieSceneCinematicShotSection.h"
#include "LevelSequence.h"

void UMoviePipelineBlueprintLibrary::GetCameraCutCounts(const UMoviePipeline* InPipeline, int32& OutTotalCuts, int32& OutCurrentCutIndex)
{
	OutTotalCuts = 0;
	OutCurrentCutIndex = 0;
	
	if(!InPipeline)
	{
		return;
	}

	const TArray<FMoviePipelineShotInfo>&  ShotList = InPipeline->GetShotList();
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

int32 UMoviePipelineBlueprintLibrary::GetOutputFrameCountEstimate(const FMoviePipelineCameraCutInfo& InCameraCut)
{
	return InCameraCut.GetOutputFrameCountEstimate().Value;
}

int32 UMoviePipelineBlueprintLibrary::GetTemporalFrameCountEstimate(const FMoviePipelineCameraCutInfo& InCameraCut)
{
	return InCameraCut.GetTemporalFrameCountEstimate().Value;
}

int32 UMoviePipelineBlueprintLibrary::GetUtilityFrameCountEstimate(const FMoviePipelineCameraCutInfo& InCameraCut)
{
	return InCameraCut.GetUtilityFrameCountEstimate().Value;
}

int32 UMoviePipelineBlueprintLibrary::GetSampleCountEstimate(const FMoviePipelineCameraCutInfo& InCameraCut, const bool bIncludeWarmup, const bool bIncludeMotionBlur)
{
	return InCameraCut.GetSampleCountEstimate(bIncludeWarmup, bIncludeMotionBlur).Value;
}

UMovieSceneSequence* UMoviePipelineBlueprintLibrary::DuplicateSequence(UObject* Outer, UMovieSceneSequence* InSequence)
{
	if (!InSequence)
	{
		return nullptr;
	}

	FObjectDuplicationParameters DuplicationParams(InSequence, Outer);
	DuplicationParams.DestName = MakeUniqueObjectName(Outer, UMovieSceneSequence::StaticClass(), InSequence->GetFName());

	// Duplicate the given sequence.
	ULevelSequence* DuplicatedSequence = (ULevelSequence*)StaticDuplicateObjectEx(DuplicationParams);

	// Now go through looking for Shot and SubSequence tracks. These currently point to the same (shared) sequence as the InSequence.
	TArray<UMovieSceneSection*> AllSubSequenceSections;

	UMovieSceneCinematicShotTrack* ShotTrack = DuplicatedSequence->GetMovieScene()->FindMasterTrack<UMovieSceneCinematicShotTrack>();
	if (ShotTrack)
	{
		AllSubSequenceSections.Append(ShotTrack->GetAllSections());
	}

	UMovieSceneSubTrack* SubTrack = DuplicatedSequence->GetMovieScene()->FindMasterTrack<UMovieSceneSubTrack>();
	if (SubTrack)
	{
		AllSubSequenceSections.Append(SubTrack->GetAllSections());
	}

	for (UMovieSceneSection* Section : AllSubSequenceSections)
	{
		UMovieSceneSubSection* SubSequenceSection = CastChecked<UMovieSceneSubSection>(Section);
		UMovieSceneSequence* OriginalSubSequence = SubSequenceSection->GetSequence();

		// Recursively duplicate this sequence, and then update our sequence to point to the duplicated one.
		UMovieSceneSequence* DuplicatedSubSequence = DuplicateSequence(Outer, OriginalSubSequence);
		SubSequenceSection->SetSequence(DuplicatedSubSequence);
	}

	return DuplicatedSequence;
}

/*
FMoviePipelineShotInfo UMoviePipeline::GetCurrentShotSnapshot() const
{
	if (CurrentShotIndex >= 0 && CurrentShotIndex < ShotList.Num())
	{
		return ShotList[CurrentShotIndex];
	}

	return FMoviePipelineShotInfo();
}

FMoviePipelineCameraCutInfo UMoviePipeline::GetCurrentCameraCutSnapshot() const
{
	FMoviePipelineShotInfo CurrentShot = GetCurrentShotSnapshot();
	if (CurrentShot.CurrentCameraCutIndex >= 0 && CurrentShot.CurrentCameraCutIndex < CurrentShot.CameraCuts.Num())
	{
		return CurrentShot.GetCurrentCameraCut();
	}

	return FMoviePipelineCameraCutInfo();
}


FMoviePipelineFrameOutputState UMoviePipeline::GetOutputStateSnapshot() const
{
	return CachedOutputState;
}*/

/*
FFrameNumber UMoviePipeline::GetTotalOutputFrameCountEstimate() const
{
	FFrameNumber EstimatedFrameCount = FFrameNumber(0);

	for (const FMoviePipelineShotInfo& Shot : ShotList)
	{
		for (const FMoviePipelineCameraCutInfo& CameraCut : Shot.CameraCuts)
		{
			EstimatedFrameCount += CameraCut.GetOutputFrameCountEstimate();
		}
	}

	return EstimatedFrameCount;
}

bool UMoviePipeline::GetRemainingTimeEstimate(FTimespan& OutTimespan) const
{
	// If they haven't produced a single frame yet, we can't give an estimate.
	if (CachedOutputState.TotalSamplesRendered <= 0)
	{
		OutTimespan = FTimespan();
		return false;
	}

	// Look at how many total samples we expect across all shots. This includes
	// samples produced for warm-ups, motion blur fixes, and temporal/spatial samples.
	FFrameNumber TotalExpectedSamples = FFrameNumber(0);

	for (const FMoviePipelineShotInfo& Shot : ShotList)
	{
		for (const FMoviePipelineCameraCutInfo& CameraCut : Shot.CameraCuts)
		{
			TotalExpectedSamples += CameraCut.GetSampleCountEstimate(true, true);
		}
	}

	// Check to see how many frames we've rendered vs. our estimate.
	int32 RenderedFrames = CachedOutputState.TotalSamplesRendered;
	int32 TotalFrames = TotalExpectedSamples.Value;

	double CompletionPercentage = FMath::Clamp(RenderedFrames / (double)TotalFrames, 0.0, 1.0);
	FTimespan CurrentDuration = FDateTime::UtcNow() - InitializationTime;

	// If it has taken us CurrentDuration to process CompletionPercentage frames, then we can get a total duration
	// estimate by taking (CurrentDuration/CompletionPercentage) and then take that total estimate minus elapsed
	// to get remaining.
	FTimespan EstimatedTotalDuration = CurrentDuration / CompletionPercentage;
	OutTimespan = EstimatedTotalDuration - CurrentDuration;

	return true;
}
*/