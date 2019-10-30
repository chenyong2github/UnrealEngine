// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MoviePipelineLinearExecutor.h"
#include "MoviePipelineNewProcessExecutor.generated.h"

/**
* This is the implementation responsible for executing the rendering of
* multiple movie pipelines on the local machine in an external process.
* This simply handles launching and managing the external processes and 
* acts as a proxy to them where possible. This internally uses the
* UMoviePipelineInProcessExecutor on the launched instances.
*/
UCLASS(Blueprintable)
class MOVIERENDERPIPELINEEDITOR_API UMoviePipelineNewProcessExecutor : public UMoviePipelineLinearExecutorBase
{
	GENERATED_BODY()
	
public:
	UMoviePipelineNewProcessExecutor()
		: UMoviePipelineLinearExecutorBase()
	{
	}
};