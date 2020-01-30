// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MoviePipelineExecutor.h"
#include "MovieRenderPipelineDataTypes.h"
#include "MoviePipelineLinearExecutor.generated.h"

class UMoviePipeline;
class UMoviePipelineQueue;

/**
* This is an abstract base class designed for executing an array of movie pipelines in linear
* fashion. It is generally the case that you only want to execute one at a time on a local machine
* and a different executor approach should be taken for a render farm that distributes out jobs
* to many different machines.
*/
UCLASS(Blueprintable, Abstract)
class MOVIERENDERPIPELINECORE_API UMoviePipelineLinearExecutorBase : public UMoviePipelineExecutorBase
{
	GENERATED_BODY()
	
public:
	UMoviePipelineLinearExecutorBase()
		: UMoviePipelineExecutorBase()
		, CurrentPipelineIndex(0)
	{
	}

protected:
	virtual void ExecuteImpl(UMoviePipelineQueue* InPipelineQueue) override;

	virtual void StartPipelineByIndex(int32 InPipelineIndex);
	virtual void Start(const UMoviePipelineExecutorJob* InJob) {}

public:
	virtual void OnIndividualPipelineFinished(UMoviePipeline* /* FinishedPipeline */);
	virtual void OnExecutorFinishedImpl();
	virtual void OnPipelineErrored(UMoviePipeline* InPipeline, bool bIsFatal, FText ErrorText);
protected:
	
	/** List of Pipeline Configs we've been asked to execute. */
	UPROPERTY(Transient)
	UMoviePipelineQueue* Queue;

	/** A Movie Pipeline that has been spawned and is running (if any) */
	UPROPERTY(Transient)
	UMoviePipeline* ActiveMoviePipeline;

	/** Which Pipeline Config are we currently working on. */
	int32 CurrentPipelineIndex;

private:
	/** Used to track total processing duration. */
	FDateTime InitializationTime;
};


