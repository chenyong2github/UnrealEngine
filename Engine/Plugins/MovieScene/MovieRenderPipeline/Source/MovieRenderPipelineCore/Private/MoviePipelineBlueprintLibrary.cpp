// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineBlueprintLibrary.h"
#include "MoviePipeline.h"
#include "MovieRenderPipelineDataTypes.h"
#include "MovieSceneSequence.h"
#include "MovieSceneSpawnable.h"
#include "MovieScenePossessable.h"
#include "MovieSceneBinding.h"
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
#include "MoviePipelineQueue.h"
#include "MoviePipelineOutputSetting.h"
#include "HAL/FileManager.h"
#include "Internationalization/Regex.h"

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
		if (ShotIndex < InMoviePipeline->GetActiveShotList().Num())
		{
			return InMoviePipeline->GetActiveShotList()[ShotIndex]->ShotInfo.State;
		}
	}

	return EMovieRenderShotState::Uninitialized;
}

FText UMoviePipelineBlueprintLibrary::GetJobName(UMoviePipeline* InMoviePipeline)
{
	if (InMoviePipeline)
	{
		return FText::FromString(InMoviePipeline->GetCurrentJob()->JobName);
	}

	return FText();
}

FText UMoviePipelineBlueprintLibrary::GetJobAuthor(UMoviePipeline* InMoviePipeline)
{
	if (InMoviePipeline)
	{
		return FText::FromString(InMoviePipeline->GetCurrentJob()->Author);
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

		for (const UMoviePipelineExecutorShot* Shot : InMoviePipeline->GetActiveShotList())
		{
			OutTotalCount += Shot->ShotInfo.WorkMetrics.TotalOutputFrameCount;
		}
	}
}

void UMoviePipelineBlueprintLibrary::GetCurrentSegmentName(UMoviePipeline* InMoviePipeline, FText& OutOuterName, FText& OutInnerName)
{
	if (InMoviePipeline)
	{
		int32 ShotIndex = InMoviePipeline->GetCurrentShotIndex();
		if (ShotIndex < InMoviePipeline->GetActiveShotList().Num())
		{
			OutOuterName = FText::FromString(InMoviePipeline->GetActiveShotList()[ShotIndex]->OuterName);
			OutInnerName = FText::FromString(InMoviePipeline->GetActiveShotList()[ShotIndex]->InnerName);
		}
	}
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
		if (ShotIndex < InMoviePipeline->GetActiveShotList().Num())
		{
			return InMoviePipeline->GetActiveShotList()[ShotIndex]->ShotInfo.WorkMetrics;
		}
	}

	return FMoviePipelineSegmentWorkMetrics();
}

void UMoviePipelineBlueprintLibrary::GetOverallSegmentCounts(const UMoviePipeline* InMoviePipeline, int32& OutCurrentIndex, int32& OutTotalCount)
{
	OutCurrentIndex = InMoviePipeline->GetCurrentShotIndex();
	OutTotalCount = InMoviePipeline->GetActiveShotList().Num();
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

static void CreateExecutorShotsFromMovieScene(UMovieScene* InMovieScene, const TRange<FFrameNumber>& InIntersectionRange, const FMovieSceneSequenceTransform& InOuterToInnerTransform, UMoviePipelineExecutorJob* InJob, UMovieSceneCinematicShotSection* InSection, const bool bForceDisable, TArray<UMoviePipelineExecutorShot*>& OutShots)
{
	check(InMovieScene && InJob);
	bool bAddedShot = false;
	
	// We're going to search for Camera Cut tracks within this shot. If none are found, we'll use the whole range of the shot.
	const UMovieSceneCameraCutTrack* CameraCutTrack = Cast<UMovieSceneCameraCutTrack>(InMovieScene->GetCameraCutTrack());
	if (CameraCutTrack)
	{
		TArray<UMovieSceneSection*> SortedSections = CameraCutTrack->GetAllSections();
		// Sort the camera cuts within a shot. The correct render order is required for relative frame counts to work.
		SortedSections.Sort([](const UMovieSceneSection& A, const UMovieSceneSection& B)
			{
				if (A.GetRange().HasLowerBound() && B.GetRange().HasLowerBound())
				{
					return A.GetRange().GetLowerBoundValue() < B.GetRange().GetLowerBoundValue();
				}
				return false;
			});

		for (UMovieSceneSection* Section : SortedSections)
		{
			UMovieSceneCameraCutSection* CameraCutSection = CastChecked<UMovieSceneCameraCutSection>(Section);
			bool bLocalForceDisable = bForceDisable;
			if (Section->GetRange().IsEmpty())
			{
				UE_LOG(LogMovieRenderPipeline, Warning, TEXT("Found zero-length section in CameraCutTrack: %s Skipping..."), *CameraCutSection->GetPathName());
				bLocalForceDisable = true;
			}

			// The inner shot track may extend past the outmost Playback Range. We want to respect the outmost playback range, which is passed in in global space 
			// as InIntersectionRange. We will convert that to local space and intersect it with our section range.
			TRange<FFrameNumber> PlaybackBoundsInLocal = InIntersectionRange * InOuterToInnerTransform.LinearTransform;
			if (!Section->GetRange().Overlaps(PlaybackBoundsInLocal))
			{
				UE_LOG(LogMovieRenderPipeline, Log, TEXT("Skipping camera cut section due to no overlap with playback range. CameraCutTrack: %s"), *CameraCutSection->GetPathName());
				bLocalForceDisable = true;
			}

			// Search the job to see if we have this already in the mask and disabled.
			UMoviePipelineExecutorShot* ExistingShot = nullptr;
			for (UMoviePipelineExecutorShot* Shot : InJob->ShotInfo)
			{
				if (Shot == nullptr)
				{
					UE_LOG(LogMovieRenderPipeline, Warning, TEXT("Null ShotInfo in job, ignoring..."));
					continue;
				}
				if (Shot->InnerPathKey == FSoftObjectPath(CameraCutSection) && Shot->OuterPathKey == FSoftObjectPath(InSection))
				{
					ExistingShot = Shot;
					break;
				}
			}

			if (ExistingShot && !ExistingShot->bEnabled)
			{
				UE_LOG(LogMovieRenderPipeline, Log, TEXT("Skipped adding Camera Cut %s to Shot List due to a shot render mask."), *CameraCutSection->GetPathName());
				bLocalForceDisable = true;
			}

			if (!ExistingShot)
			{
				ExistingShot = NewObject<UMoviePipelineExecutorShot>(InJob);
			}

			// It shouldn't be a duplicate but if it is then the rendering fails later so 
			// we will ensure it's not a duplicate now for less crashes.
			OutShots.AddUnique(ExistingShot);			
			ExistingShot->InnerPathKey = FSoftObjectPath(CameraCutSection);
			ExistingShot->OuterPathKey = InSection;
			if (InSection)
			{
				ExistingShot->OuterName = InSection->GetShotDisplayName();
			}

			const FMovieSceneObjectBindingID& CameraObjectBindingId = CameraCutSection->GetCameraBindingID();
			if (CameraObjectBindingId.IsValid())
			{
				const FMovieSceneBinding* Binding = InMovieScene->FindBinding(CameraObjectBindingId.GetGuid());
				if (Binding)
				{
					if (FMovieSceneSpawnable* Spawnable = InMovieScene->FindSpawnable(Binding->GetObjectGuid()))
					{
						ExistingShot->InnerName = Spawnable->GetName();
					}
					else if (FMovieScenePossessable* Posssessable = InMovieScene->FindPossessable(Binding->GetObjectGuid()))
					{
						ExistingShot->InnerName = Posssessable->GetName();
					}
					else
					{
						ExistingShot->InnerName = Binding->GetName();
					}
				}
			}

			// We need to generate a UMoviePipelineExecutorShot for each thing in a sequence regardless if the user wanted
			// to use it so that we can appropriately disable everything else that is masked off. 
			if (bLocalForceDisable)
			{
				ExistingShot->bEnabled = false;
			}
			bAddedShot = true;

			// The section range in local space as clipped by the outmost playback bounds.
			TRange<FFrameNumber> ClippedSectionRange = TRange<FFrameNumber>::Intersection(PlaybackBoundsInLocal, Section->GetRange());

			// We also want to know how much the camera cut track extended outside of the playback range to calculate
			// how many warmup frames we can do for real warmups. 
			TRange<FFrameNumber> CameraCutRange = TRange<FFrameNumber>::Empty();

			// If the camera cut sticks past our playback range (on the left side) then keep track of that.
			if (CameraCutSection->GetRange().GetLowerBoundValue() < ClippedSectionRange.GetLowerBoundValue())
			{
				CameraCutRange = TRange<FFrameNumber>(CameraCutSection->GetRange().GetLowerBoundValue(), ClippedSectionRange.GetLowerBoundValue());
			}

			// Reset the shot info (as it is all transient data) so it doesn't get re-used between runs.
			ExistingShot->ShotInfo = FMoviePipelineCameraCutInfo();
			ExistingShot->ShotInfo.CameraCutSection = CameraCutSection;
			ExistingShot->ShotInfo.OriginalRangeLocal = ClippedSectionRange;
			ExistingShot->ShotInfo.TotalOutputRangeLocal = ClippedSectionRange;
			ExistingShot->ShotInfo.WarmUpRangeLocal = CameraCutRange;
			ExistingShot->ShotInfo.InnerToOuterTransform = InOuterToInnerTransform.InverseLinearOnly();
		}
	}

	// If we didn't detect any valid camera cuts within this movie scene, we will just add the whole movie scene as a new shot.
	if (!bAddedShot)
	{
		UE_LOG(LogMovieRenderPipeline, Warning, TEXT("No Cinematic Shot Sections found in range, and no Camera Cut Tracks found. Playback Range will be used but camera will render from Pawns perspective."));

		// Search the job to see if we have this already in the mask and disabled.
		UMoviePipelineExecutorShot* ExistingShot = nullptr;
		FString InnerName;
		FString OuterName;

		// Because we didn't have any camera cuts inside of our movie scene, then, if possible, the owning section is
		// our reference. If we don't have an owning section then this is a root level movie scene being rendered and
		// we just use the outer Movie Scene.
		UObject const* OwningObject = InSection;
		if (InSection)
		{
			OuterName = InSection->GetShotDisplayName();
		}
		if(!OwningObject)
		{
			OwningObject = InMovieScene;
			InnerName = FPaths::GetBaseFilename(InMovieScene->GetPathName());
		}

		for (UMoviePipelineExecutorShot* Shot : InJob->ShotInfo)
		{
			if (Shot->InnerPathKey == nullptr && Shot->OuterPathKey == FSoftObjectPath(OwningObject))
			{
				ExistingShot = Shot;
				break;
			}
		}

		if (!ExistingShot)
		{
			ExistingShot = NewObject<UMoviePipelineExecutorShot>(InJob);
		}

		OutShots.AddUnique(ExistingShot);
		// Reset the shot info (as it is all transient data) so it doesn't get re-used between runs.
		ExistingShot->ShotInfo = FMoviePipelineCameraCutInfo();
		ExistingShot->InnerPathKey = nullptr;
		ExistingShot->OuterPathKey = FSoftObjectPath(OwningObject);
		ExistingShot->ShotInfo.CameraCutSection = nullptr;
		ExistingShot->ShotInfo.OriginalRangeLocal = InIntersectionRange * InOuterToInnerTransform.LinearTransform;
		ExistingShot->ShotInfo.TotalOutputRangeLocal = InIntersectionRange * InOuterToInnerTransform.LinearTransform;
		ExistingShot->ShotInfo.WarmUpRangeLocal = TRange<FFrameNumber>::Empty();;
		ExistingShot->ShotInfo.InnerToOuterTransform = InOuterToInnerTransform.InverseLinearOnly();
		ExistingShot->InnerName = InnerName;
		ExistingShot->OuterName = OuterName;
	}
}

void UMoviePipelineBlueprintLibrary::UpdateJobShotListFromSequence(ULevelSequence* InSequence, UMoviePipelineExecutorJob* InJob)
{
	if (!ensureMsgf(InSequence && InJob, TEXT("Cannot generate shot list for null sequence/job.")))
	{
		return;
	}
	TArray<UMoviePipelineExecutorShot*> NewShots;

	// Shot Tracks take precedent over camera cuts, as settings can only be applied as granular as a shot.
	UMovieSceneCinematicShotTrack* CinematicShotTrack = InSequence->GetMovieScene()->FindMasterTrack<UMovieSceneCinematicShotTrack>();

	if (CinematicShotTrack)
	{
		// Sort the sections by their actual location on the timeline.
		CinematicShotTrack->SortSections();
		TArray<UMovieSceneSection*> SortedSections = CinematicShotTrack->GetAllSections();

		for (UMovieSceneSection* Section : SortedSections)
		{
			UMovieSceneCinematicShotSection* ShotSection = CastChecked<UMovieSceneCinematicShotSection>(Section);
			bool bForceDisable = false;
			
			// If the user has manually marked a section as inactive we don't produce a shot for it.
			if (!Section->IsActive())
			{
				UE_LOG(LogMovieRenderPipeline, Log, TEXT("Skipped adding Shot %s to Shot List due to being inactive."), *ShotSection->GetShotDisplayName());
				bForceDisable = true;
			}

			// If the shot section points to a sequence that no longer exists...
			if (!ShotSection->GetSequence())
			{
				UE_LOG(LogMovieRenderPipeline, Warning, TEXT("Skipped adding Shot %s to Shot List due to no inner sequence."), *ShotSection->GetShotDisplayName());
				bForceDisable = true;
			}

			// Skip this section if it falls entirely outside of our playback bounds.
			TRange<FFrameNumber> MasterPlaybackBounds = InJob->GetConfiguration()->GetEffectivePlaybackRange(InSequence);
			if (!ShotSection->GetRange().Overlaps(MasterPlaybackBounds))
			{
				UE_LOG(LogMovieRenderPipeline, Log, TEXT("Skipped adding Shot %s to Shot List due to not overlapping playback bounds."), *ShotSection->GetShotDisplayName());
				bForceDisable = true;
			}

			// The Shot Section may extend past our Sequence's Playback Bounds. We intersect the two bounds to ensure that the Playback Start/Playback End of the overall sequence is respected.
			TRange<FFrameNumber> CinematicShotSectionRange = TRange<FFrameNumber>::Intersection(ShotSection->GetRange(), InSequence->GetMovieScene()->GetPlaybackRange());

			CreateExecutorShotsFromMovieScene(ShotSection->GetSequence()->GetMovieScene(), CinematicShotSectionRange, ShotSection->OuterToInnerTransform(), InJob, ShotSection, bForceDisable, /*Out*/ NewShots);
		}
	}

	if (NewShots.Num() == 0)
	{
		// They either didn't have a cinematic shot track, or it didn't have any valid sections. We will try to build sections from a camera cut at the top level,
		// or the entire top level if there is no camera cut track.
		CreateExecutorShotsFromMovieScene(InSequence->GetMovieScene(), InSequence->GetMovieScene()->GetPlaybackRange(), FMovieSceneSequenceTransform(), InJob, nullptr, false, /*Out*/ NewShots);
	}


	// Now that we've read the job's shot mask we will clear it and replace it with only things still valid.
	InJob->ShotInfo.Reset();
	InJob->ShotInfo = NewShots;
}

int32 UMoviePipelineBlueprintLibrary::ResolveVersionNumber(const UMoviePipeline* InMoviePipeline)
{
	if (!InMoviePipeline)
	{
		return -1;
	}

	UMoviePipelineOutputSetting* OutputSettings = InMoviePipeline->GetPipelineMasterConfig()->FindSetting<UMoviePipelineOutputSetting>();
	if (!OutputSettings->bAutoVersion)
	{
		return OutputSettings->VersionNumber;
	}

	// Calculate a version number by looking at the output path and then scanning for a version token.
	FString FileNameFormatString = OutputSettings->OutputDirectory.Path / OutputSettings->FileNameFormat;

	FString FinalPath;
	FMoviePipelineFormatArgs FinalFormatArgs;
	FStringFormatNamedArguments Overrides;
	Overrides.Add(TEXT("version"), TEXT("{version}")); // Force the Version string to stay as {version} so we can substring based on it later.

	InMoviePipeline->ResolveFilenameFormatArguments(FileNameFormatString, Overrides, FinalPath, FinalFormatArgs);
	FinalPath = FPaths::ConvertRelativePathToFull(FinalPath);
	FPaths::NormalizeFilename(FinalPath);

	// If they're using the version token, try to resolve the directory that the files are in.
	if (FinalPath.Contains(TEXT("{version}")))
	{
		int32 HighestVersion = 0;

		// FinalPath can have {version} either in a folder name or in a file name. We need to find the 'parent' of either the file or folder that contains it. We can do this by substringing
		// for {version} and then finding the last "/" character, which will be the containing folder.
		int32 VersionStringIndex = FinalPath.Find(TEXT("{version}"), ESearchCase::Type::IgnoreCase, ESearchDir::Type::FromStart);
		if (VersionStringIndex >= 0)
		{
			int32 LastParentFolder = FinalPath.Find(TEXT("/"), ESearchCase::Type::IgnoreCase, ESearchDir::Type::FromEnd, VersionStringIndex);
			FinalPath.LeftInline(LastParentFolder + 1);

			// Now that we have the parent folder of either the folder with the version token, or the file with the version token, we will
			// look through all immediate children and scan for version tokens so we can find the highest one.
			const FRegexPattern VersionSearchPattern(TEXT("v([0-9]{3})"));
			TArray<FString> FilesAndFoldersInDirectory;
			IFileManager& FileManager = IFileManager::Get();
			FileManager.FindFiles(FilesAndFoldersInDirectory, *(FinalPath / TEXT("*.*")), true, true);

			for (const FString& Path : FilesAndFoldersInDirectory)
			{
				FRegexMatcher Regex(VersionSearchPattern, *Path);
				if (Regex.FindNext())
				{
					FString Result = Regex.GetCaptureGroup(0);
					if (Result.Len() > 0)
					{
						// Strip the "v" token off, expected pattern is vXXX
						Result.RightChopInline(1);
					}

					int32 VersionNumber = 0;
					LexFromString(VersionNumber, *Result);
					if (VersionNumber > HighestVersion)
					{
						HighestVersion = VersionNumber;
					}
				}
			}

		}

		return  HighestVersion + 1;
	}

	return 0;
}