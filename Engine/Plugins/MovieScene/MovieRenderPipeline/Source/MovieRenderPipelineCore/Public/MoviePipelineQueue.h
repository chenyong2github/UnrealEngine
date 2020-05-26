// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "UObject/Object.h"
#include "MovieRenderPipelineDataTypes.h"
#include "LevelSequence.h"
#include "MoviePipelineMasterConfig.h"
#include "MoviePipelineConfigBase.h"

#include "MoviePipelineQueue.generated.h"

class UMoviePipelineMasterConfig;
class ULevel;
class ULevelSequence;

/**
* A particular job within the Queue
*/
UCLASS(BlueprintType)
class MOVIERENDERPIPELINECORE_API UMoviePipelineExecutorJob : public UObject
{
	GENERATED_BODY()
public:
	UMoviePipelineExecutorJob()
	{
		Configuration = CreateDefaultSubobject<UMoviePipelineMasterConfig>("DefaultConfig");
	}

	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	bool HasFinished() const
	{
		return JobStatus == EMoviePipelineExecutorJobStatus::Finished;
	}

	float GetProgressPercentage() const
	{
		return 0.2f;
	}

public:	
	UFUNCTION(BlueprintCallable, Category = "Movie Render Pipeline")
	void SetPresetOrigin(UMoviePipelineMasterConfig* InPreset);

	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	UMoviePipelineMasterConfig* GetPresetOrigin() const
	{
		return PresetOrigin.Get();
	}

	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline")
	UMoviePipelineMasterConfig* GetConfiguration() const
	{
		return Configuration;
	}

	UFUNCTION(BlueprintCallable, Category = "Movie Render Pipeline")
	void SetConfiguration(UMoviePipelineMasterConfig* InPreset);

	UFUNCTION(BlueprintSetter, Category = "Movie Render Pipeline")
	void SetSequence(FSoftObjectPath InSequence);

public:
	// UObject Interface
	void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent);
	// ~UObject Interface

public:
	/** (Optional) Name of the job. Shown on the default burn-in. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Movie Render Pipeline")
	FText JobName;

	/** Which sequence should this job render? */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, BlueprintSetter = "SetSequence", Category = "Movie Render Pipeline", meta = (AllowedClasses = "LevelSequence"))
	FSoftObjectPath Sequence;

	/** Which map should this job render on */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Movie Render Pipeline", meta = (AllowedClasses = "World"))
	FSoftObjectPath Map;

	/** (Optional) Name of the person who submitted the job. Can be shown in burn in as a first point of contact about the content. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Movie Render Pipeline")
	FText Author;

	/** (Optional) Shot specific information. If a shot is missing from this list it will assume to be enabled and will be rendered. */
	UPROPERTY(BlueprintReadWrite, Category = "Movie Render Pipeline")
	TArray<FMoviePipelineJobShotInfo> ShotMaskInfo;

	/** What state is this particular job instance currently in? */
	UPROPERTY(BlueprintReadOnly, Category = "Movie Render Pipeline")
	EMoviePipelineExecutorJobStatus JobStatus;

private:
	/** 
	*/
	UPROPERTY(Instanced)
	UMoviePipelineMasterConfig* Configuration;

	/**
	*/
	UPROPERTY()
	TSoftObjectPtr<UMoviePipelineMasterConfig> PresetOrigin;
};

/**
* A queue is a list of jobs that have been executed, are executing and are waiting to be executed. These can be saved
* to specific assets to allow 
*/
UCLASS(BlueprintType)
class MOVIERENDERPIPELINECORE_API UMoviePipelineQueue : public UObject
{
	GENERATED_BODY()
public:
	
	UMoviePipelineQueue()
		: QueueSerialNumber(0)
	{
		// Ensure instances are always transactional
		if (!HasAnyFlags(RF_ClassDefaultObject))
		{
			SetFlags(RF_Transactional);
		}
	}
	
	/**
	* Allocates a new Job in this Queue. The Queue owns the jobs for memory management purposes,
	* and this will handle that for you. 
	*
	* @param InJobType	Specify the specific Job type that should be created. Custom Executors can use custom Job types to allow the user to provide more information.
	* @return	The created Executor job instance.
	*/
	UFUNCTION(BlueprintCallable, meta = (DeterminesOutputType = "InClass"), Category = "Movie Render Pipeline|Queue", meta=(InJobType="/Script/MovieRenderPipelineCore.MoviePipelineExecutorJob"))
	UMoviePipelineExecutorJob* AllocateNewJob(TSubclassOf<UMoviePipelineExecutorJob> InJobType);

	/**
	* Deletes the specified job from the Queue. 
	*
	* @param InJob	The job to look for and delete. 
	*/
	UFUNCTION(BlueprintCallable, Category = "Movie Render Pipeline|Queue")
	void DeleteJob(UMoviePipelineExecutorJob* InJob);

	/**
	* Duplicate the specific job and return the duplicate. Configurations are duplicated and not shared.
	*
	* @param InJob	The job to look for to duplicate.
	* @return The duplicated instance or nullptr if a duplicate could not be made.
	*/
	UFUNCTION(BlueprintCallable, Category = "Movie Render Pipeline|Queue")
	UMoviePipelineExecutorJob* DuplicateJob(UMoviePipelineExecutorJob* InJob);
	
	/**
	* Get all of the Jobs contained in this Queue.
	* @return The jobs contained by this queue.
	*/
	UFUNCTION(BlueprintPure, Category = "Movie Render Pipeline|Queue")
	TArray<UMoviePipelineExecutorJob*> GetJobs() const
	{
		return Jobs;
	}
	
	/**
	 * Retrieve the serial number that is incremented when a job is added or removed from this list.
	 * @note: This field is not serialized, and not copied along with UObject duplication.
	 */
	uint32 GetQueueSerialNumber() const
	{
		return QueueSerialNumber;
	}

private:
	UPROPERTY()
	TArray<UMoviePipelineExecutorJob*> Jobs;
	
private:
	int32 QueueSerialNumber;
};