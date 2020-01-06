// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
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
	{
	}

protected:
	virtual void Start(const UMoviePipelineExecutorJob* InJob) override;

private:
	void OnMapLoadFinished(UWorld* NewWorld);
	void OnMoviePipelineFinished(UMoviePipeline* InMoviePipeline);
};