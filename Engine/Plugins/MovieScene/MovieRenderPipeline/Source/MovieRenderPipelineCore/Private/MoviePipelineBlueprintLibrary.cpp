// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineBlueprintLibrary.h"
#include "MoviePipeline.h"
#include "MovieRenderPipelineDataTypes.h"
#include "MovieSceneSequence.h"
#include "LevelSequence.h"
#include "UObject/SoftObjectPath.h"
#include "MoviePipelineQueue.h"
#include "MovieRenderPipelineCoreModule.h"
#include "Tracks/MovieSceneSubTrack.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Tracks/MovieSceneCinematicShotTrack.h"
#include "Sections/MovieSceneCinematicShotSection.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "MovieSceneTimeHelpers.h"

EMovieRenderPipelineState UMoviePipelineBlueprintLibrary::GetPipelineState(const UMoviePipeline* InPipeline)
{
	if (InPipeline)
	{
		return InPipeline->GetPipelineState();
	}

	return EMovieRenderPipelineState::Uninitialized;
}

EMovieRenderShotState UMoviePipelineBlueprintLibrary::GetCurrentSegmentState(UMoviePipeline* InMoviePipeline)
{
	if (InMoviePipeline)
	{
		int32 ShotIndex = InMoviePipeline->GetCurrentShotIndex();
		if (ShotIndex < InMoviePipeline->GetShotList().Num())
		{
			return InMoviePipeline->GetShotList()[ShotIndex].GetCurrentCameraCut().State;
		}
	}

	return EMovieRenderShotState::Uninitialized;
}

FText UMoviePipelineBlueprintLibrary::GetJobName(UMoviePipeline* InMoviePipeline)
{
	if (InMoviePipeline)
	{
		return InMoviePipeline->GetCurrentJob()->JobName;
	}

	return FText();
}

FText UMoviePipelineBlueprintLibrary::GetJobAuthor(UMoviePipeline* InMoviePipeline)
{
	if (InMoviePipeline)
	{
		return InMoviePipeline->GetCurrentJob()->Author;
	}

	return FText();
}

void UMoviePipelineBlueprintLibrary::GetOverallOutputFrames(const UMoviePipeline* InMoviePipeline, int32& OutCurrentIndex, int32& OutTotalCount)
{
	OutCurrentIndex = 0;
	OutTotalCount = 0;
	if (InMoviePipeline)
	{
		OutCurrentIndex = InMoviePipeline->GetOutputState().OutputFrameNumber;

		for (const FMoviePipelineShotInfo& Shot : InMoviePipeline->GetShotList())
		{
			for (const FMoviePipelineCameraCutInfo& Segment : Shot.CameraCuts)
			{
				OutTotalCount += Segment.WorkMetrics.TotalOutputFrameCount;
			}
		}
	}
}

FText UMoviePipelineBlueprintLibrary::GetCurrentSegmentName(UMoviePipeline* InMoviePipeline)
{
	if (InMoviePipeline)
	{
		int32 ShotIndex = InMoviePipeline->GetCurrentShotIndex();
		if (ShotIndex < InMoviePipeline->GetShotList().Num())
		{
			return FText::FromString(InMoviePipeline->GetShotList()[ShotIndex].GetCurrentCameraCut().ShotName);
		}
	}

	return FText();
}

FDateTime UMoviePipelineBlueprintLibrary::GetJobInitializationTime(const UMoviePipeline* InMoviePipeline)
{
	if (InMoviePipeline)
	{
		return InMoviePipeline->GetInitializationTime();
	}

	return FDateTime();
}

FMoviePipelineSegmentWorkMetrics UMoviePipelineBlueprintLibrary::GetCurrentSegmentWorkMetrics(const UMoviePipeline* InMoviePipeline)
{
	if(InMoviePipeline)
	{
		int32 ShotIndex = InMoviePipeline->GetCurrentShotIndex();
		if (ShotIndex < InMoviePipeline->GetShotList().Num())
		{
			return InMoviePipeline->GetShotList()[ShotIndex].GetCurrentCameraCut().WorkMetrics;
		}
	}

	return FMoviePipelineSegmentWorkMetrics();
}

void UMoviePipelineBlueprintLibrary::GetOverallSegmentCounts(const UMoviePipeline* InMoviePipeline, int32& OutCurrentIndex, int32& OutTotalCount)
{
	OutCurrentIndex = 0;
	OutTotalCount = 0;

	if (InMoviePipeline)
	{
		int32 RunningSegmentCount = 0;
		for (int32 ShotIndex = 0; ShotIndex < InMoviePipeline->GetShotList().Num(); ShotIndex++)
		{
			const FMoviePipelineShotInfo& Shot = InMoviePipeline->GetShotList()[ShotIndex];

			OutTotalCount += Shot.CameraCuts.Num();
			
			// If we've already fully rendered this shot, then we just add the number of segments
			if (ShotIndex < InMoviePipeline->GetCurrentShotIndex())
			{
				OutCurrentIndex += Shot.CameraCuts.Num();
			}
			// If we're partway through this shot only add the index
			else if (ShotIndex == InMoviePipeline->GetCurrentShotIndex())
			{
				OutCurrentIndex += Shot.CurrentCameraCutIndex;
			}
			else
			{
				// It's a future shot/cut so don't add it.
			}
		}
	}
}

UMovieSceneSequence* UMoviePipelineBlueprintLibrary::DuplicateSequence(UObject* Outer, UMovieSceneSequence* InSequence)
{
	if (!InSequence)
	{
		return nullptr;
	}

	FObjectDuplicationParameters DuplicationParams(InSequence, Outer);
	DuplicationParams.DestName = MakeUniqueObjectName(Outer, UMovieSceneSequence::StaticClass());

	// Duplicate the given sequence.
	UMovieSceneSequence* DuplicatedSequence = CastChecked<UMovieSceneSequence>(StaticDuplicateObjectEx(DuplicationParams));

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

bool UMoviePipelineBlueprintLibrary::GetEstimatedTimeRemaining(const UMoviePipeline* InPipeline, FTimespan& OutEstimate)
{
	if (!InPipeline)
	{
		OutEstimate = FTimespan();
		return false;
	}

	// If they haven't produced a single frame yet, we can't give an estimate.
	int32 OutputFrames;
	int32 TotalOutputFrames;
	GetOverallOutputFrames(InPipeline, OutputFrames, TotalOutputFrames);

	if (OutputFrames <= 0 || TotalOutputFrames <= 0)
	{
		OutEstimate = FTimespan();
		return false;
	}

	float CompletionPercentage = OutputFrames / float(TotalOutputFrames);;
	FTimespan CurrentDuration = FDateTime::UtcNow() - InPipeline->GetInitializationTime();

	// If it has taken us CurrentDuration to process CompletionPercentage samples, then we can get a total duration
	// estimate by taking (CurrentDuration/CompletionPercentage) and then take that total estimate minus elapsed
	// to get remaining.
	FTimespan EstimatedTotalDuration = CurrentDuration / CompletionPercentage;
	OutEstimate = EstimatedTotalDuration - CurrentDuration;

	return true;
}

float UMoviePipelineBlueprintLibrary::GetCompletionPercentage(const UMoviePipeline* InPipeline)
{
	if (!InPipeline)
	{
		return 0.f;
	}

	int32 OutputFrames;
	int32 TotalOutputFrames;
	GetOverallOutputFrames(InPipeline, OutputFrames, TotalOutputFrames);

	float CompletionPercentage = FMath::Clamp(OutputFrames / (float)TotalOutputFrames, 0.f, 1.f);
	return CompletionPercentage;
}

FTimecode UMoviePipelineBlueprintLibrary::GetMasterTimecode(const UMoviePipeline* InMoviePipeline)
{
	if (InMoviePipeline)
	{
		return InMoviePipeline->GetOutputState().SourceTimeCode;
	}

	return FTimecode();
}

FFrameNumber UMoviePipelineBlueprintLibrary::GetMasterFrameNumber(const UMoviePipeline* InMoviePipeline)
{
	if (InMoviePipeline)
	{
		if (InMoviePipeline->GetTargetSequence() && InMoviePipeline->GetTargetSequence()->GetMovieScene())
		{
			FFrameRate EffectiveFrameRate = InMoviePipeline->GetPipelineMasterConfig()->GetEffectiveFrameRate(InMoviePipeline->GetTargetSequence());
			return GetMasterTimecode(InMoviePipeline).ToFrameNumber(EffectiveFrameRate);
		}
	}

	return FFrameNumber(-1);
}

FTimecode UMoviePipelineBlueprintLibrary::GetCurrentShotTimecode(const UMoviePipeline* InMoviePipeline)
{
	if (InMoviePipeline)
	{
		return InMoviePipeline->GetOutputState().CurrentShotSourceTimeCode;
	}

	return FTimecode();
}

FFrameNumber UMoviePipelineBlueprintLibrary::GetCurrentShotFrameNumber(const UMoviePipeline* InMoviePipeline)
{
	if (InMoviePipeline)
	{
		FFrameRate EffectiveFrameRate = InMoviePipeline->GetPipelineMasterConfig()->GetEffectiveFrameRate(InMoviePipeline->GetTargetSequence());
		return GetCurrentShotTimecode(InMoviePipeline).ToFrameNumber(EffectiveFrameRate);
	}

	return FFrameNumber(-1);
}


FString UMoviePipelineBlueprintLibrary::GetMapPackageName(UMoviePipelineExecutorJob* InJob)
{
	if (!InJob)
	{
		return FString();
	}

	return InJob->Map.GetLongPackageName();
}

static void CreateShotMaskFromMovieScene(UMovieScene* InMovieScene, const TRange<FFrameNumber>& InIntersectionRange, UMovieSceneSubSection* InSubSection, TArray<FMoviePipelineJobShotInfo>& OutShotInfo)
{
	const UMovieSceneCameraCutTrack* CameraCutTrack = Cast<UMovieSceneCameraCutTrack>(InMovieScene->GetCameraCutTrack());
	if (CameraCutTrack)
	{
		TArray<UMovieSceneSection*> SortedSections = CameraCutTrack->GetAllSections();
		SortedSections.Sort([](const UMovieSceneSection& A, const UMovieSceneSection& B)
			{
				if (A.SectionRange.GetLowerBound().IsClosed() && B.SectionRange.GetLowerBound().IsClosed())
				{
					return A.SectionRange.GetLowerBound().GetValue() < B.SectionRange.GetLowerBound().GetValue();
				}
				return false;
			});

		for (UMovieSceneSection* Section : SortedSections)
		{
			UMovieSceneCameraCutSection* CameraCutSection = CastChecked<UMovieSceneCameraCutSection>(Section);
			if (Section->GetRange().IsEmpty())
			{
				UE_LOG(LogMovieRenderPipeline, Warning, TEXT("Found zero-length section in CameraCutTrack: %s Skipping..."), *CameraCutSection->GetPathName());
				continue;
			}

			TRange<FFrameNumber> SectionRangeInMaster = Section->GetRange();

			// If this camera cut track is inside of a shot subsection, we need to take the parent section into account.
			if (InSubSection)
			{
				TRange<FFrameNumber> LocalSectionRange = Section->GetRange(); // Section in local space
				LocalSectionRange = MovieScene::TranslateRange(LocalSectionRange, -InMovieScene->GetPlaybackRange().GetLowerBoundValue()); // Section relative to zero
				SectionRangeInMaster = MovieScene::TranslateRange(LocalSectionRange, InSubSection->GetRange().GetLowerBoundValue()); // Convert to master sequence space.
			}

			if (!SectionRangeInMaster.Overlaps(InIntersectionRange))
			{
				UE_LOG(LogMovieRenderPipeline, Verbose, TEXT("Skipping camera cut section due to no overlap with playback range. CameraCutTrack: %s"), *CameraCutSection->GetPathName());
				continue;
			}

			FMoviePipelineJobShotInfo& ShotInfo = OutShotInfo.AddDefaulted_GetRef();
			ShotInfo.bEnabled = CameraCutSection->IsActive();
			ShotInfo.SectionPath = FSoftObjectPath(CameraCutSection);
		}
	}
}

TArray<FMoviePipelineJobShotInfo> UMoviePipelineBlueprintLibrary::CreateShotMask(const UMoviePipelineExecutorJob* InJob)
{
	// This mirrors the logic of UMoviePipeline::BuildShotListFromSequence for now.
	TArray<FMoviePipelineJobShotInfo> ShotInfos;

	if (!InJob)
	{
		return ShotInfos;
	}

	ULevelSequence* Sequence = Cast<ULevelSequence>(InJob->Sequence.TryLoad());
	if (!Sequence)
	{
		return ShotInfos;
	}

	// Prioritize shot-tracks over camera cuts for each sequence.
	UMovieSceneCinematicShotTrack* CinematicShotTrack = Sequence->GetMovieScene()->FindMasterTrack<UMovieSceneCinematicShotTrack>();
	if (CinematicShotTrack)
	{
		TArray<UMovieSceneSection*> SortedSections = CinematicShotTrack->GetAllSections();
		SortedSections.Sort([](UMovieSceneSection& A, UMovieSceneSection& B)
			{
				if (A.SectionRange.GetLowerBound().IsClosed() && B.SectionRange.GetLowerBound().IsClosed())
				{
					return A.SectionRange.GetLowerBound().GetValue() < B.SectionRange.GetLowerBound().GetValue();
				}
				return false;
			});
	

		for (UMovieSceneSection* Section : SortedSections)
		{
			UMovieSceneCinematicShotSection* ShotSection = CastChecked<UMovieSceneCinematicShotSection>(Section);

			// If the user has manually marked a section as inactive we don't produce a shot for it.
			if (!Section->IsActive())
			{
				UE_LOG(LogMovieRenderPipeline, Verbose, TEXT("Skipped adding Shot %s to Shot List due to being inactive."), *ShotSection->GetShotDisplayName());
				continue;
			}

			if (!ShotSection->GetSequence())
			{
				UE_LOG(LogMovieRenderPipeline, Warning, TEXT("Skipped adding Shot %s to Shot List due to no inner sequence."), *ShotSection->GetShotDisplayName());
				continue;
			}

			// Skip this section if it falls entirely outside of our playback bounds.
			TRange<FFrameNumber> MasterPlaybackBounds = InJob->GetConfiguration()->GetEffectivePlaybackRange(Sequence);
			if (!ShotSection->GetRange().Overlaps(MasterPlaybackBounds))
			{
				UE_LOG(LogMovieRenderPipeline, Verbose, TEXT("Skipped adding Shot %s to Shot List due to not overlapping playback bounds."), *ShotSection->GetShotDisplayName());
				continue;
			}

			// The Shot Section may extend past our Sequence's Playback Bounds. We intersect the two bounds to ensure that
			// the Playback Start/Playback End of the overall sequence is respected.
			TRange<FFrameNumber> CinematicShotSectionRange = TRange<FFrameNumber>::Intersection(ShotSection->GetRange(), Sequence->GetMovieScene()->GetPlaybackRange());
			CreateShotMaskFromMovieScene(ShotSection->GetSequence()->GetMovieScene(), CinematicShotSectionRange, ShotSection, ShotInfos);
		}
	}

	return ShotInfos;
}