// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MoviePipelineExecutor.h"
#include "MoviePipelineLinearExecutor.generated.h"

class UMoviePipeline;
class UMoviePipelineInEditorExecution;

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
	virtual void ExecuteImpl(TArray<UMovieRenderPipelineConfig*>& InPipelines) override;

	virtual void StartPipelineByIndex(int32 InPipelineIndex);
	virtual void Start(UMovieRenderPipelineConfig* InConfig, const int32 InConfigIndex, const int32 InNumConfigs) { check(InConfig); }

public:
	virtual void OnIndividualPipelineFinished(UMoviePipeline* /* FinishedPipeline */);
	virtual void OnExecutorFinishedImpl(const bool bInSuccess);
protected:
	
	/** List of Pipeline Configs we've been asked to execute. */
	UPROPERTY(Transient)
	TArray<UMovieRenderPipelineConfig*> Pipelines;

	/** Which Pipeline Config are we currently working on. */
	int32 CurrentPipelineIndex;

	/** Instance of the Pipeline that exists in the world that is currently processing (if any) */
	UPROPERTY(Transient)
	UMoviePipeline* ActiveMoviePipeline;

	/** Instance of the Config we're supposed to be working on */
	UPROPERTY(Transient)
	UMovieRenderPipelineConfig* ActiveConfig;

private:
	/** Used to track total processing duration. */
	FDateTime InitializationTime;
};


