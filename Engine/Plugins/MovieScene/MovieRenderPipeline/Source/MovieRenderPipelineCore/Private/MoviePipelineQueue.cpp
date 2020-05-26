// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineQueue.h"
#include "MoviePipelineOutputSetting.h"
#include "MoviePipelineSetting.h"
#include "MoviePipelineBlueprintLibrary.h"

UMoviePipelineExecutorJob* UMoviePipelineQueue::AllocateNewJob()
{
#if WITH_EDITOR
	Modify();
#endif

	UMoviePipelineExecutorJob* NewJob = NewObject<UMoviePipelineExecutorJob>(this);
	NewJob->SetFlags(RF_Transactional);

	Jobs.Add(NewJob);
	QueueSerialNumber++;

	return NewJob;
}

void UMoviePipelineQueue::DeleteJob(UMoviePipelineExecutorJob* InJob)
{
	if (!InJob)
	{
		return;
	}

#if WITH_EDITOR
	Modify();
#endif

	Jobs.Remove(InJob);
	QueueSerialNumber++;
}

UMoviePipelineExecutorJob* UMoviePipelineQueue::DuplicateJob(UMoviePipelineExecutorJob* InJob)
{
	if (!InJob)
	{
		return nullptr;
	}

#if WITH_EDITOR
	Modify();
#endif

	UMoviePipelineExecutorJob* NewJob = CastChecked<UMoviePipelineExecutorJob>(StaticDuplicateObject(InJob, this));
	Jobs.Add(NewJob);

	QueueSerialNumber++;
	return NewJob;
}

void UMoviePipelineExecutorJob::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UMoviePipelineExecutorJob, Sequence))
	{
		// Call our Set function so that we rebuild the shot mask.
		SetSequence(Sequence);
	}
}

void UMoviePipelineExecutorJob::SetSequence(FSoftObjectPath InSequence)
{
	Sequence = InSequence;

	// Rebuild our shot mask.
	ShotMaskInfo.Reset();

	ULevelSequence* LoadedSequence = Cast<ULevelSequence>(Sequence.TryLoad());
	if (!LoadedSequence)
	{
		return;
	}

	ShotMaskInfo = UMoviePipelineBlueprintLibrary::CreateShotMask(this);
}

void UMoviePipelineExecutorJob::SetConfiguration(UMoviePipelineMasterConfig* InPreset)
{
	if (InPreset)
	{
		Configuration->CopyFrom(InPreset);
		PresetOrigin = nullptr;
	}
}

void UMoviePipelineExecutorJob::SetPresetOrigin(UMoviePipelineMasterConfig* InPreset)
{
	if (InPreset)
	{
		Configuration->CopyFrom(InPreset);
		PresetOrigin = TSoftObjectPtr<UMoviePipelineMasterConfig>(InPreset);
	}
}