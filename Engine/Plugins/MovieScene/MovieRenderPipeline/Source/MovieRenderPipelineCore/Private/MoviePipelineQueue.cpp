// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineQueue.h"
#include "MoviePipelineOutputSetting.h"
#include "MoviePipelineSetting.h"

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