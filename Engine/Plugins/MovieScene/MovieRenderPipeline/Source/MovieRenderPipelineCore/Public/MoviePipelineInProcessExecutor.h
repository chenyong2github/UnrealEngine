// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MoviePipelineLinearExecutor.h"
#include "MoviePipelineInProcessExecutor.generated.h"

/**
* This executor implementation can process an array of movie pipelines and
* run them inside the currently running process. This is intended for usage
* outside of the editor (ie. -game mode) as it will take over the currently
* running world/game instance instead of launching a new world instance like 
* the editor only PIE.
*/
UCLASS(Blueprintable)
class MOVIERENDERPIPELINECORE_API UMoviePipelineInProcessExecutor : public UMoviePipelineLinearExecutorBase
{
	GENERATED_BODY()

public:
	UMoviePipelineInProcessExecutor()
		: UMoviePipelineLinearExecutorBase()
		, RemainingInitializationFrames(-1)
	{
	}

protected:
	virtual void Start(const UMoviePipelineExecutorJob* InJob) override;
	virtual void OnIndividualPipelineFinished(UMoviePipeline* FinishedPipeline) override;

private:
	void OnMapLoadFinished(UWorld* NewWorld);
	void OnMoviePipelineFinished(UMoviePipeline* InMoviePipeline);
	void OnApplicationQuit();
	void OnTick();

private:
	/** If using delayed initialization, how many frames are left before we call Initialize. Will be -1 if not actively counting down. */
	int32 RemainingInitializationFrames;
};